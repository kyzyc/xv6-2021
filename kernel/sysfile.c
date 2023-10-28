//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

static int checkVMAAddr(uint64 addr, uint64 len);
uint64
sys_mmap(void)
{
  uint64 addr;
  uint64 len, off; 
  int prot, flags;
  struct file* file;

  if (argaddr(0, &addr) < 0) {
    return -1;
  }

  if (argint(1, (int*)&len) < 0 || argint(2, &prot) < 0 || argint(3, &flags) < 0) {
    return -1;
  }

  if (argfd(4, 0, &file) < 0 || argint(5, (int*)&off) < 0) {
    return -1;
  }

  // only consider address is zero
  if (addr != 0) {
    return -1;
  }

  // check file permission
  if (prot & PROT_READ) {
    if (!file->readable) {
      return -1;
    }
  }
  if (prot & PROT_WRITE) {
    if (!(flags & MAP_PRIVATE) && !file->writable) {
      return -1;
    }
  }

  struct proc* p = myproc();

  addr = VMASTART;

  for (int i = 0; i < p->vmaIndex; ++i) {
    if (checkVMAAddr(addr, len)) {
      break;
    } else {
      // printf("should!\n");
      addr = PGROUNDUP(p->VMA[i].addr + p->VMA[i].len);
    }
  }
  // add vma to process VMA array
  // printf("prot: %d\n", prot); 
  if (p->vmaIndex < NOVMA) {
    p->VMA[p->vmaIndex].addr = addr;
    p->VMA[p->vmaIndex].len = len;
    p->VMA[p->vmaIndex].off = off;
    p->VMA[p->vmaIndex].prot = prot;
    p->VMA[p->vmaIndex].flags = flags;
    p->VMA[p->vmaIndex].file = file;
    filedup(file);                // increase the file's reference count
    p->vmaIndex++;
  } else {
    return -1;
  }
  
  return addr;
}

static int
findVMAIndex(uint64 addr, uint64 len, int* Index) 
{
  struct proc* p = myproc();

  struct vma* VMA;
  for (int i = 0; i < p->vmaIndex; ++i) {
    VMA = &p->VMA[i];
    if ((addr >= VMA->addr) && ((addr + len) <= (VMA->addr + VMA->len))) {
      // printf("yes!\n");
      *Index = i;
      return 1;
    }
    // printf("%p %p %p %p\n", addr, VMA.addr, addr + len, VMA.addr + VMA.len);
  }

  return 0;
}

static void
moveAllVMA(int Index)
{
  struct proc* p = myproc();

  int end = p->vmaIndex;
  for (int i = Index; i < end - 1; ++i) {
    p->VMA[i].addr = p->VMA[i + 1].addr;
    p->VMA[i].len = p->VMA[i + 1].len;
    p->VMA[i].off = p->VMA[i + 1].off;
    p->VMA[i].flags = p->VMA[i + 1].flags;
    p->VMA[i].file = p->VMA[i + 1].file;
    p->VMA[i].prot = p->VMA[i + 1].prot;
  }
}

uint64
sys_munmap(void)
{
  uint64 addr;
  int len;
  if (argaddr(0, &addr) < 0 || argint(1, &len) < 0) {
    return -1;
  }

  int vmaIndex;
  struct proc* p = myproc();

  if (findVMAIndex(addr, len, &vmaIndex)) {
    // should delete whole vma
    if (p->VMA[vmaIndex].addr == addr && p->VMA[vmaIndex].len == len) {
      if (p->VMA[vmaIndex].flags & MAP_SHARED) {
        // write back to file
        filewrite(p->VMA[vmaIndex].file, p->VMA[vmaIndex].addr, len);
      }
      // unmap the address range 
      int freePages = PGROUNDUP(p->VMA[vmaIndex].len) / PGSIZE;
      uvmunmap(p->pagetable, PGROUNDDOWN(p->VMA[vmaIndex].addr), freePages, 1);
      // decrement the reference count of the corresponding struct file
      fileclose(p->VMA[vmaIndex].file);
      // move whole VMA
      moveAllVMA(vmaIndex);
      // decrement vmaIndex
      p->vmaIndex--; 
      return 0;
    } else {
      if (p->VMA[vmaIndex].flags & MAP_SHARED) {
        // write back to file
        filewrite(p->VMA[vmaIndex].file, p->VMA[vmaIndex].addr, len);
      }
      // only consider start of vma seciton or end of vma section
      // start of vma section
      if (p->VMA[vmaIndex].addr == addr) {
        p->VMA[vmaIndex].addr += len;
      }
      p->VMA[vmaIndex].len -= len;
      // unmap the address range 
      int freePages = PGROUNDUP(len) / PGSIZE;
      uvmunmap(p->pagetable, PGROUNDDOWN(addr), freePages, 1);
      return 0;
    }
  } else {
    printf("error!\n");
    printf("%p %d\n", addr, len);
    return -1;
  }
}

static int
checkVMAAddr(uint64 addr, uint64 len)
{
  struct proc* p = myproc();
  struct vma* VMA;
  for (int i = 0; i < p->vmaIndex; ++i) {
    VMA = &p->VMA[i];
    if (addr >= VMA->addr && addr < PGROUNDUP(VMA->addr + VMA->len)) {
      return 0;
    }
    if ((addr + len) >= VMA->addr && (addr + len) < PGROUNDUP(VMA->addr + VMA->len)) {
      return 0;
    }
  }
  return 1;
}
