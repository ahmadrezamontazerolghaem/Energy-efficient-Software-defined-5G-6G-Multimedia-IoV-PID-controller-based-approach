#include <linux/export.h>
#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/mman.h>
#include <linux/reboot.h>
#include <linux/prctl.h>
#include <linux/highuid.h>
#include <linuxv/fs.h> 


#include <linux/kmod.h> 
#include <linux/perf_event.h>
#include <linux/resource.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/capability.h>
#include <linux/device.h>
#include <linux/key.h>
#include <linux/times.h>
#include <linux/posix-timers.h>
#include <linux/security.h>
#include <linux/dcookies.h>
#include <linux/suspend.h>
#include <linux/tty.h>
#include <linux/signal.h>
#include <linux/cn_proc.h>
#include <linux/getcpu.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/seccomp.h>
#include <linux/cpu.h>
#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/fs_struct.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/gfp.h>
#include <linux/syscore_ops.h>
#include <linux/version.h>
#include <linux/ctype.h>
#include <linux/compat.h>
#include <linux/syscalls.h>
#include <linux/kprobes.h>
#include <linux/user_namespace.h>
#include <linux/binfmts.h>

#include <linux/sched.h>
#include <linux/rcupdate.h>
#include <linux/uidgid.h>
#include <linux/cred.h>

#include <linux/kmsg_dump.h>
/* Move somewhere else to avoid recompiling? */
#include <generated/utsrelease.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/unistd.h>

#ifndef SET_UNALIGN_CTL
# define SET_UNALIGN_CTL(a, b)	(-EINVAL)
#endif
#ifndef GET_UNALIGN_CTL
# define GET_UNALIGN_CTL(a, b)	(-EINVAL)
#endif
#ifndef SET_FPEMU_CTL
# define SET_FPEMU_CTL(a, b)	(-EINVAL)
#endif
#ifndef GET_FPEMU_CTL
# define GET_FPEMU_CTL(a, b)	(-EINVAL)
#endif
#ifndef SET_FPEXC_CTL
# define SET_FPEXC_CTL(a, b)	(-EINVAL)
#endif
#ifndef GET_FPEXC_CTL
# define GET_FPEXC_CTL(a, b)	(-EINVAL)
#endif
#ifndef GET_ENDIAN
# define GET_ENDIAN(a, b)	(-EINVAL)
#endif
#ifndef SET_ENDIAN
# define SET_ENDIAN(a, b)	(-EINVAL)
#endif
#ifndef GET_TSC_CTL
# define GET_TSC_CTL(a)		(-EINVAL)
#endif
#ifndef SET_TSC_CTL
# define SET_TSC_CTL(a)		(-EINVAL)
#endif
#ifndef MPX_ENABLE_MANAGEMENT
# define MPX_ENABLE_MANAGEMENT(a)	(-EINVAL)
#endif
#ifndef MPX_DISABLE_MANAGEMENT
# define MPX_DISABLE_MANAGEMENT(a)	(-EINVAL)
#endif
#ifndef GET_FP_MODE
# define GET_FP_MODE(a)		(-EINVAL)
#endif
#ifndef SET_FP_MODE
# define SET_FP_MODE(a,b)	(-EINVAL)
#endif

/*
 * this is where the system-wide overflow UID and GID are defined, for
 * architectures that now have 32-bit UID/GID but didn't in the past
 */

int overflowuid = DEFAULT_OVERFLOWUID;
int overflowgid = DEFAULT_OVERFLOWGID;

EXPORT_SYMBOL(overflowuid);
EXPORT_SYMBOL(overflowgid);

/*
 * the same as above, but for filesystems which can only store a 16-bit
 * UID and GID. as such, this is needed on all architectures
 */

int fs_overflowuid = DEFAULT_FS_OVERFLOWUID;
int fs_overflowgid = DEFAULT_FS_OVERFLOWUID;

EXPORT_SYMBOL(fs_overflowuid);
EXPORT_SYMBOL(fs_overflowgid);

/*
 * Returns true if current's euid is same as p's uid or euid,
 * or has CAP_SYS_NICE to p's user_ns.
 *
 * Called with rcu_read_lock, creds are safe
 */
static bool set_one_prio_perm(struct task_struct *p)
{
	const struct cred *cred = current_cred(), *pcred = __task_cred(p);

	if (uid_eq(pcred->uid,  cred->euid) ||
	    uid_eq(pcred->euid, cred->euid))
		return true;
	if (ns_capable(pcred->user_ns, CAP_SYS_NICE))
		return true;
	return false;
}

/*
 * set the priority of a task
 * - the caller must hold the RCU read lock
 */
static int set_one_prio(struct task_struct *p, int niceval, int error)
{
	int no_nice;

	if (!set_one_prio_perm(p)) {
		error = -EPERM;
		goto out;
	}
	if (niceval < task_nice(p) && !can_nice(p, niceval)) {
		error = -EACCES;
		goto out;
	}
	no_nice = security_task_setnice(p, niceval);
	if (no_nice) {
		error = no_nice;
		goto out;
	}
	if (error == -ESRCH)
		error = 0;
	set_user_nice(p, niceval);
out:
	return error;
}

SYSCALL_DEFINE3(setpriority, int, which, int, who, int, niceval)
{
	struct task_struct *g, *p;
	struct user_struct *user;
	const struct cred *cred = current_cred();
	int error = -EINVAL;
	struct pid *pgrp;
	kuid_t uid;

	if (which > PRIO_USER || which < PRIO_PROCESS)
		goto out;

	/* normalize: avoid signed division (rounding problems) */
	error = -ESRCH;
	if (niceval < MIN_NICE)
		niceval = MIN_NICE;
	if (niceval > MAX_NICE)
		niceval = MAX_NICE;

	rcu_read_lock();
	read_lock(&tasklist_lock);
	switch (which) {
	case PRIO_PROCESS:
		if (who)
			p = find_task_by_vpid(who);
		else
			p = current;
		if (p)
			error = set_one_prio(p, niceval, error);
		break;
	case PRIO_PGRP:
		if (who)
			pgrp = find_vpid(who);
		else
			pgrp = task_pgrp(current);
		do_each_pid_thread(pgrp, PIDTYPE_PGID, p) {
			error = set_one_prio(p, niceval, error);
		} while_each_pid_thread(pgrp, PIDTYPE_PGID, p);
		break;
	case PRIO_USER:
		uid = make_kuid(cred->user_ns, who);
		user = cred->user;
		if (!who)
			uid = cred->uid;
		else if (!uid_eq(uid, cred->uid)) {
			user = find_user(uid);
			if (!user)
				goto out_unlock;	/* No processes for this user */
		}
		do_each_thread(g, p) {
			if (uid_eq(task_uid(p), uid))
				error = set_one_prio(p, niceval, error);
		} while_each_thread(g, p);
		if (!uid_eq(uid, cred->uid))
			free_uid(user);		/* For find_user() */
		break;
	}
out_unlock:
	read_unlock(&tasklist_lock);
	rcu_read_unlock();
out:
	return error;
}

/*
 * Ugh. To avoid negative return values, "getpriority()" will
 * not return the normal nice-value, but a negated value that
 * has been offset by 20 (ie it returns 40..1 instead of -20..19)
 * to stay compatible.
 */
SYSCALL_DEFINE2(getpriority, int, which, int, who)
{
	struct task_struct *g, *p;
	struct user_struct *user;
	const struct cred *cred = current_cred();
	long niceval, retval = -ESRCH;
	struct pid *pgrp;
	kuid_t uid;

	if (which > PRIO_USER || which < PRIO_PROCESS)
		return -EINVAL;

	rcu_read_lock();
	read_lock(&tasklist_lock);
	switch (which) {
	case PRIO_PROCESS:
		if (who)
			p = find_task_by_vpid(who);
		else
			p = current;
		if (p) {
			niceval = nice_to_rlimit(task_nice(p));
			if (niceval > retval)
				retval = niceval;
		}
		break;
	case PRIO_PGRP:
		if (who)
			pgrp = find_vpid(who);
		else
			pgrp = task_pgrp(current);
		do_each_pid_thread(pgrp, PIDTYPE_PGID, p) {
			niceval = nice_to_rlimit(task_nice(p));
			if (niceval > retval)
				retval = niceval;
		} while_each_pid_thread(pgrp, PIDTYPE_PGID, p);
		break;
	case PRIO_USER:
		uid = make_kuid(cred->user_ns, who);
		user = cred->user;
		if (!who)
			uid = cred->uid;
		else if (!uid_eq(uid, cred->uid)) {
			user = find_user(uid);
			if (!user)
				goto out_unlock;	/* No processes for this user */
		}
		do_each_thread(g, p) {
			if (uid_eq(task_uid(p), uid)) {
				niceval = nice_to_rlimit(task_nice(p));
				if (niceval > retval)
					retval = niceval;
			}
		} while_each_thread(g, p);
		if (!uid_eq(uid, cred->uid))
			free_uid(user);		/* for find_user() */
		break;
	}
out_unlock:
	read_unlock(&tasklist_lock);
	rcu_read_unlock();

	return retval;
}

/*
 * Unprivileged users may change the real gid to the effective gid
 * or vice versa.  (BSD-style)
 *
 * If you set the real gid at all, or set the effective gid to a value not
 * equal to the real gid, then the saved gid is set to the new effective gid.
 *
 * This makes it possible for a setgid program to completely drop its
 * privileges, which is often a useful assertion to make when you are doing
 * a security audit over a program.
 *
 * The general idea is that a program which uses just setregid() will be
 * 100% compatible with BSD.  A program which uses just setgid() will be
 * 100% compatible with POSIX with saved IDs.
 *
 * SMP: There are not races, the GIDs are checked only by filesystem
 *      operations (as far as semantic preservation is concerned).
 */
#ifdef CONFIG_MULTIUSER
SYSCALL_DEFINE2(setregid, gid_t, rgid, gid_t, egid)
{
	struct user_namespace *ns = current_user_ns();
	const struct cred *old;
	struct cred *new;
	int retval;
	kgid_t krgid, kegid;

	krgid = make_kgid(ns, rgid);
	kegid = make_kgid(ns, egid);

	if ((rgid != (gid_t) -1) && !gid_valid(krgid))
		return -EINVAL;
	if ((egid != (gid_t) -1) && !gid_valid(kegid))
		return -EINVAL;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;
	old = current_cred();

	retval = -EPERM;
	if (rgid != (gid_t) -1) {
		if (gid_eq(old->gid, krgid) ||
		    gid_eq(old->egid, krgid) ||
		    ns_capable(old->user_ns, CAP_SETGID))
			new->gid = krgid;
		else
			goto error;
	}
	if (egid != (gid_t) -1) {
		if (gid_eq(old->gid, kegid) ||
		    gid_eq(old->egid, kegid) ||
		    gid_eq(old->sgid, kegid) ||
		    ns_capable(old->user_ns, CAP_SETGID))
			new->egid = kegid;
		else
			goto error;
	}

	if (rgid != (gid_t) -1 ||
	    (egid != (gid_t) -1 && !gid_eq(kegid, old->gid)))
		new->sgid = new->egid;
	new->fsgid = new->egid;

	return commit_creds(new);

error:
	abort_creds(new);
	return retval;
}

/*
 * setgid() is implemented like SysV w/ SAVED_IDS
 *
 * SMP: Same implicit races as above.
 */
SYSCALL_DEFINE1(setgid, gid_t, gid)
{
	struct user_namespace *ns = current_user_ns();
	const struct cred *old;
	struct cred *new;
	int retval;
	kgid_t kgid;

	kgid = make_kgid(ns, gid);
	if (!gid_valid(kgid))
		return -EINVAL;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;
	old = current_cred();

	retval = -EPERM;
	if (ns_capable(old->user_ns, CAP_SETGID))
		new->gid = new->egid = new->sgid = new->fsgid = kgid;
	else if (gid_eq(kgid, old->gid) || gid_eq(kgid, old->sgid))
		new->egid = new->fsgid = kgid;
	else
		goto error;

	return commit_creds(new);

error:
	abort_creds(new);
	return retval;
}

/*
 * change the user struct in a credentials set to match the new UID
 */
static int set_user(struct cred *new)
{
	struct user_struct *new_user;

	new_user = alloc_uid(new->uid);
	if (!new_user)
		return -EAGAIN;

	/*
	 * We don't fail in case of NPROC limit excess here because too many
	 * poorly written programs don't check set*uid() return code, assuming
	 * it never fails if called by root.  We may still enforce NPROC limit
	 * for programs doing set*uid()+execve() by harmlessly deferring the
	 * failure to the execve() stage.
	 */
	if (atomic_read(&new_user->processes) >= rlimit(RLIMIT_NPROC) &&
			new_user != INIT_USER)
		current->flags |= PF_NPROC_EXCEEDED;
	else
		current->flags &= ~PF_NPROC_EXCEEDED;

	free_uid(new->user);
	new->user = new_user;
	return 0;
}

/*
 * Unprivileged users may change the real uid to the effective uid
 * or vice versa.  (BSD-style)
 *
 * If you set the real uid at all, or set the effective uid to a value not
 * equal to the real uid, then the saved uid is set to the new effective uid.
 *
 * This makes it possible for a setuid program to completely drop its
 * privileges, which is often a useful assertion to make when you are doing
 * a security audit over a program.
 *
 * The general idea is that a program which uses just setreuid() will be
 * 100% compatible with BSD.  A program which uses just setuid() will be
 * 100% compatible with POSIX with saved IDs.
 */
SYSCALL_DEFINE2(setreuid, uid_t, ruid, uid_t, euid)
{
	struct user_namespace *ns = current_user_ns();
	const struct cred *old;
	struct cred *new;
	int retval;
	kuid_t kruid, keuid;

	kruid = make_kuid(ns, ruid);
	keuid = make_kuid(ns, euid);

	if ((ruid != (uid_t) -1) && !uid_valid(kruid))
		return -EINVAL;
	if ((euid != (uid_t) -1) && !uid_valid(keuid))
		return -EINVAL;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;
	old = current_cred();

	retval = -EPERM;
	if (ruid != (uid_t) -1) {
		new->uid = kruid;
		if (!uid_eq(old->uid, kruid) &&
		    !uid_eq(old->euid, kruid) &&
		    !ns_capable(old->user_ns, CAP_SETUID))
			goto error;
	}

	if (euid != (uid_t) -1) {
		new->euid = keuid;
		if (!uid_eq(old->uid, keuid) &&
		    !uid_eq(old->euid, keuid) &&
		    !uid_eq(old->suid, keuid) &&
		    !ns_capable(old->user_ns, CAP_SETUID))
			goto error;
	}

	if (!uid_eq(new->uid, old->uid)) {
		retval = set_user(new);
		if (retval < 0)
			goto error;
	}
	if (ruid != (uid_t) -1 ||
	    (euid != (uid_t) -1 && !uid_eq(keuid, old->uid)))
		new->suid = new->euid;
	new->fsuid = new->euid;

	retval = security_task_fix_setuid(new, old, LSM_SETID_RE);
	if (retval < 0)
		goto error;

	return commit_creds(new);

error:
	abort_creds(new);
	return retval;
}

/*
 * setuid() is implemented like SysV with SAVED_IDS
 *
 * Note that SAVED_ID's is deficient in that a setuid root program
 * like sendmail, for example, cannot set its uid to be a normal
 * user and then switch back, because if you're root, setuid() sets
 * the saved uid too.  If you don't like this, blame the bright people
 * in the POSIX committee and/or USG.  Note that the BSD-style setreuid()
 * will allow a root program to temporarily drop privileges and be able to
 * regain them by swapping the real and effective uid.
 */
SYSCALL_DEFINE1(setuid, uid_t, uid)
{
	struct user_namespace *ns = current_user_ns();
	const struct cred *old;
	struct cred *new;
	int retval;
	kuid_t kuid;

	kuid = make_kuid(ns, uid);
	if (!uid_valid(kuid))
		return -EINVAL;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;
	old = current_cred();

	retval = -EPERM;
	if (ns_capable(old->user_ns, CAP_SETUID)) {
		new->suid = new->uid = kuid;
		if (!uid_eq(kuid, old->uid)) {
			retval = set_user(new);
			if (retval < 0)
				goto error;
		}
	} else if (!uid_eq(kuid, old->uid) && !uid_eq(kuid, new->suid)) {
		goto error;
	}

	new->fsuid = new->euid = kuid;

	retval = security_task_fix_setuid(new, old, LSM_SETID_ID);
	if (retval < 0)
		goto error;

	return commit_creds(new);

error:
	abort_creds(new);
	return retval;
}


/*
 * This function implements a generic ability to update ruid, euid,
 * and suid.  This allows you to implement the 4.4 compatible seteuid().
 */
SYSCALL_DEFINE3(setresuid, uid_t, ruid, uid_t, euid, uid_t, suid)
{
	struct user_namespace *ns = current_user_ns();
	const struct cred *old;
	struct cred *new;
	int retval;
	kuid_t kruid, keuid, ksuid;

	kruid = make_kuid(ns, ruid);
	keuid = make_kuid(ns, euid);
	ksuid = make_kuid(ns, suid);

	if ((ruid != (uid_t) -1) && !uid_valid(kruid))
		return -EINVAL;

	if ((euid != (uid_t) -1) && !uid_valid(keuid))
		return -EINVAL;

	if ((suid != (uid_t) -1) && !uid_valid(ksuid))
		return -EINVAL;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	old = current_cred();

	retval = -EPERM;
	if (!ns_capable(old->user_ns, CAP_SETUID)) {
		if (ruid != (uid_t) -1        && !uid_eq(kruid, old->uid) &&
		    !uid_eq(kruid, old->euid) && !uid_eq(kruid, old->suid))
			goto error;
		if (euid != (uid_t) -1        && !uid_eq(keuid, old->uid) &&
		    !uid_eq(keuid, old->euid) && !uid_eq(keuid, old->suid))
			goto error;
		if (suid != (uid_t) -1        && !uid_eq(ksuid, old->uid) &&
		    !uid_eq(ksuid, old->euid) && !uid_eq(ksuid, old->suid))
			goto error;
	}

	if (ruid != (uid_t) -1) {
		new->uid = kruid;
		if (!uid_eq(kruid, old->uid)) {
			retval = set_user(new);
			if (retval < 0)
				goto error;
		}
	}
	if (euid != (uid_t) -1)
		new->euid = keuid;
	if (suid != (uid_t) -1)
		new->suid = ksuid;
	new->fsuid = new->euid;

	retval = security_task_fix_setuid(new, old, LSM_SETID_RES);
	if (retval < 0)
		goto error;

	return commit_creds(new);

error:
	abort_creds(new);
	return retval;
}

SYSCALL_DEFINE3(getresuid, uid_t __user *, ruidp, uid_t __user *, euidp, uid_t __user *, suidp)
{
	const struct cred *cred = current_cred();
	int retval;
	uid_t ruid, euid, suid;

	ruid = from_kuid_munged(cred->user_ns, cred->uid);
	euid = from_kuid_munged(cred->user_ns, cred->euid);
	suid = from_kuid_munged(cred->user_ns, cred->suid);

	retval = put_user(ruid, ruidp);
	if (!retval) {
		retval = put_user(euid, euidp);
		if (!retval)
			return put_user(suid, suidp);
	}
	return retval;
}

/*
 * Same as above, but for rgid, egid, sgid.
 */
SYSCALL_DEFINE3(setresgid, gid_t, rgid, gid_t, egid, gid_t, sgid)
{
	struct user_namespace *ns = current_user_ns();
	const struct cred *old;
	struct cred *new;
	int retval;
	kgid_t krgid, kegid, ksgid;

	krgid = make_kgid(ns, rgid);
	kegid = make_kgid(ns, egid);
	ksgid = make_kgid(ns, sgid);

	if ((rgid != (gid_t) -1) && !gid_valid(krgid))
		return -EINVAL;
	if ((egid != (gid_t) -1) && !gid_valid(kegid))
		return -EINVAL;
	if ((sgid != (gid_t) -1) && !gid_valid(ksgid))
		return -EINVAL;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;
	old = current_cred();

	retval = -EPERM;
	if (!ns_capable(old->user_ns, CAP_SETGID)) {
		if (rgid != (gid_t) -1        && !gid_eq(krgid, old->gid) &&
		    !gid_eq(krgid, old->egid) && !gid_eq(krgid, old->sgid))
			goto error;
		if (egid != (gid_t) -1        && !gid_eq(kegid, old->gid) &&
		    !gid_eq(kegid, old->egid) && !gid_eq(kegid, old->sgid))
			goto error;
		if (sgid != (gid_t) -1        && !gid_eq(ksgid, old->gid) &&
		    !gid_eq(ksgid, old->egid) && !gid_eq(ksgid, old->sgid))
			goto error;
	}

	if (rgid != (gid_t) -1)
		new->gid = krgid;
	if (egid != (gid_t) -1)
		new->egid = kegid;
	if (sgid != (gid_t) -1)
		new->sgid = ksgid;
	new->fsgid = new->egid;

	return commit_creds(new);

error:
	abort_creds(new);
	return retval;
}

SYSCALL_DEFINE3(getresgid, gid_t __user *, rgidp, gid_t __user *, egidp, gid_t __user *, sgidp)
{
	const struct cred *cred = current_cred();
	int retval;
	gid_t rgid, egid, sgid;

	rgid = from_kgid_munged(cred->user_ns, cred->gid);
	egid = from_kgid_munged(cred->user_ns, cred->egid);
	sgid = from_kgid_munged(cred->user_ns, cred->sgid);

	retval = put_user(rgid, rgidp);
	if (!retval) {
		retval = put_user(egid, egidp);
		if (!retval)
			retval = put_user(sgid, sgidp);
	}

	return retval;
}


/*
 * "setfsuid()" sets the fsuid - the uid used for filesystem checks. This
 * is used for "access()" and for the NFS daemon (letting nfsd stay at
 * whatever uid it wants to). It normally shadows "euid", except when
 * explicitly set by setfsuid() or for access..
 */
SYSCALL_DEFINE1(setfsuid, uid_t, uid)
{
	const struct cred *old;
	struct cred *new;
	uid_t old_fsuid;
	kuid_t kuid;

	old = current_cred();
	old_fsuid = from_kuid_munged(old->user_ns, old->fsuid);

	kuid = make_kuid(old->user_ns, uid);
	if (!uid_valid(kuid))
		return old_fsuid;

	new = prepare_creds();
	if (!new)
		return old_fsuid;

	if (uid_eq(kuid, old->uid)  || uid_eq(kuid, old->euid)  ||
	    uid_eq(kuid, old->suid) || uid_eq(kuid, old->fsuid) ||
	    ns_capable(old->user_ns, CAP_SETUID)) {
		if (!uid_eq(kuid, old->fsuid)) {
			new->fsuid = kuid;
			if (security_task_fix_setuid(new, old, LSM_SETID_FS) == 0)
				goto change_okay;
		}
	}

	abort_creds(new);
	return old_fsuid;

change_okay:
	commit_creds(new);
	return old_fsuid;
}

/*
 * Samma på svenska..
 */
SYSCALL_DEFINE1(setfsgid, gid_t, gid)
{
	const struct cred *old;
	struct cred *new;
	gid_t old_fsgid;
	kgid_t kgid;

	old = current_cred();
	old_fsgid = from_kgid_munged(old->user_ns, old->fsgid);

	kgid = make_kgid(old->user_ns, gid);
	if (!gid_valid(kgid))
		return old_fsgid;

	new = prepare_creds();
	if (!new)
		return old_fsgid;

	if (gid_eq(kgid, old->gid)  || gid_eq(kgid, old->egid)  ||
	    gid_eq(kgid, old->sgid) || gid_eq(kgid, old->fsgid) ||
	    ns_capable(old->user_ns, CAP_SETGID)) {
		if (!gid_eq(kgid, old->fsgid)) {
			new->fsgid = kgid;
			goto change_okay;
		}
	}

	abort_creds(new);
	return old_fsgid;

change_okay:
	commit_creds(new);
	return old_fsgid;
}
#endif /* CONFIG_MULTIUSER */

/**
 * sys_getpid - return the thread group id of the current process
 *
 * Note, despite the name, this returns the tgid not the pid.  The tgid and
 * the pid are identical unless CLONE_THREAD was specified on clone() in
 * which case the tgid is the same in all threads of the same group.
 *
 * This is SMP safe as current->tgid does not change.
 */
SYSCALL_DEFINE0(getpid)
{
	return task_tgid_vnr(current);
}

/* Thread ID - the internal kernel "pid" */
SYSCALL_DEFINE0(gettid)
{
	return task_pid_vnr(current);
}

/*
 * Accessing ->real_parent is not SMP-safe, it could
 * change from under us. However, we can use a stale
 * value of ->real_parent under rcu_read_lock(), see
 * release_task()->call_rcu(delayed_put_task_struct).
 */
SYSCALL_DEFINE0(getppid)
{
	int pid;

	rcu_read_lock();
	pid = task_tgid_vnr(rcu_dereference(current->real_parent));
	rcu_read_unlock();

	return pid;
}

SYSCALL_DEFINE0(getuid)
{
	/* Only we change this so SMP safe */
	return from_kuid_munged(current_user_ns(), current_uid());
}

SYSCALL_DEFINE0(geteuid)
{
	/* Only we change this so SMP safe */
	return from_kuid_munged(current_user_ns(), current_euid());
}

SYSCALL_DEFINE0(getgid)
{
	/* Only we change this so SMP safe */
	return from_kgid_munged(current_user_ns(), current_gid());
}

SYSCALL_DEFINE0(getegid)
{
	/* Only we change this so SMP safe */
	return from_kgid_munged(current_user_ns(), current_egid());
}

void do_sys_times(struct tms *tms)
{
	cputime_t tgutime, tgstime, cutime, cstime;

	thread_group_cputime_adjusted(current, &tgutime, &tgstime);
	cutime = current->signal->cutime;
	cstime = current->signal->cstime;
	tms->tms_utime = cputime_to_clock_t(tgutime);
	tms->tms_stime = cputime_to_clock_t(tgstime);
	tms->tms_cutime = cputime_to_clock_t(cutime);
	tms->tms_cstime = cputime_to_clock_t(cstime);
}

SYSCALL_DEFINE1(times, struct tms __user *, tbuf)
{
	if (tbuf) {
		struct tms tmp;

		do_sys_times(&tmp);
		if (copy_to_user(tbuf, &tmp, sizeof(struct tms)))
			return -EFAULT;
	}
	force_successful_syscall_return();
	return (long) jiffies_64_to_clock_t(get_jiffies_64());
}

/*
 * This needs some heavy checking ...
 * I just haven't the stomach for it. I also don't fully
 * understand sessions/pgrp etc. Let somebody who does explain it.
 *
 * OK, I think I have the protection semantics right.... this is really
 * only important on a multi-user system anyway, to make sure one user
 * can't send a signal to a process owned by another.  -TYT, 12/12/91
 *
 * !PF_FORKNOEXEC check to conform completely to POSIX.
 */
SYSCALL_DEFINE2(setpgid, pid_t, pid, pid_t, pgid)
{
	struct task_struct *p;
	struct task_struct *group_leader = current->group_leader;
	struct pid *pgrp;
	int err;

	if (!pid)
		pid = task_pid_vnr(group_leader);
	if (!pgid)
		pgid = pid;
	if (pgid < 0)
		return -EINVAL;
	rcu_read_lock();

	/* From this point forward we keep holding onto the tasklist lock
	 * so that our parent does not change from under us. -DaveM
	 */
	write_lock_irq(&tasklist_lock);

	err = -ESRCH;
	p = find_task_by_vpid(pid);
	if (!p)
		goto out;

	err = -EINVAL;
	if (!thread_group_leader(p))
		goto out;

	if (same_thread_group(p->real_parent, group_leader)) {
		err = -EPERM;
		if (task_session(p) != task_session(group_leader))
			goto out;
		err = -EACCES;
		if (!(p->flags & PF_FORKNOEXEC))
			goto out;
	} else {
		err = -ESRCH;
		if (p != group_leader)
			goto out;
	}

	err = -EPERM;
	if (p->signal->leader)
		goto out;

	pgrp = task_pid(p);
	if (pgid != pid) {
		struct task_struct *g;

		pgrp = find_vpid(pgid);
		g = pid_task(pgrp, PIDTYPE_PGID);
		if (!g || task_session(g) != task_session(group_leader))
			goto out;
	}

	err = security_task_setpgid(p, pgid);
	if (err)
		goto out;

	if (task_pgrp(p) != pgrp)
		change_pid(p, PIDTYPE_PGID, pgrp);

	err = 0;
out:
	/* All paths lead to here, thus we are safe. -DaveM */
	write_unlock_irq(&tasklist_lock);
	rcu_read_unlock();
	return err;
}

SYSCALL_DEFINE1(getpgid, pid_t, pid)
{
	struct task_struct *p;
	struct pid *grp;
	int retval;

	rcu_read_lock();
	if (!pid)
		grp = task_pgrp(current);
	else {
		retval = -ESRCH;
		p = find_task_by_vpid(pid);
		if (!p)
			goto out;
		grp = task_pgrp(p);
		if (!grp)
			goto out;

		retval = security_task_getpgid(p);
		if (retval)
			goto out;
	}
	retval = pid_vnr(grp);
out:
	rcu_read_unlock();
	return retval;
}

#ifdef __ARCH_WANT_SYS_GETPGRP

SYSCALL_DEFINE0(getpgrp)
{
	return sys_getpgid(0);
}

#endif

SYSCALL_DEFINE1(getsid, pid_t, pid)
{
	struct task_struct *p;
	struct pid *sid;
	int retval;

	rcu_read_lock();
	if (!pid)
		sid = task_session(current);
	else {
		retval = -ESRCH;
		p = find_task_by_vpid(pid);
		if (!p)
			goto out;
		sid = task_session(p);
		if (!sid)
			goto out;

		retval = security_task_getsid(p);
		if (retval)
			goto out;
	}
	retval = pid_vnr(sid);
out:
	rcu_read_unlock();
	return retval;
}

static void set_special_pids(struct pid *pid)
{
	struct task_struct *curr = current->group_leader;

	if (task_session(curr) != pid)
		change_pid(curr, PIDTYPE_SID, pid);

	if (task_pgrp(curr) != pid)
		change_pid(curr, PIDTYPE_PGID, pid);
}

SYSCALL_DEFINE0(setsid)
{
	struct task_struct *group_leader = current->group_leader;
	struct pid *sid = task_pid(group_leader);
	pid_t session = pid_vnr(sid);
	int err = -EPERM;

	write_lock_irq(&tasklist_lock);
	/* Fail if I am already a session leader */
	if (group_leader->signal->leader)
		goto out;

	/* Fail if a process group id already exists that equals the
	 * proposed session id.
	 */
	if (pid_task(sid, PIDTYPE_PGID))
		goto out;

	group_leader->signal->leader = 1;
	set_special_pids(sid);

	proc_clear_tty(group_leader);

	err = session;
out:
	write_unlock_irq(&tasklist_lock);
	if (err > 0) {
		proc_sid_connector(group_leader);
		sched_autogroup_create_attach(group_leader);
	}
	return err;
}

DECLARE_RWSEM(uts_sem);

#ifdef COMPAT_UTS_MACHINE
#define override_architecture(name) \
	(personality(current->personality) == PER_LINUX32 && \
	 copy_to_user(name->machine, COMPAT_UTS_MACHINE, \
		      sizeof(COMPAT_UTS_MACHINE)))
#else
#define override_architecture(name)	0
#endif

/*
 * Work around broken programs that cannot handle "Linux 3.0".
 * Instead we map 3.x to 2.6.40+x, so e.g. 3.0 would be 2.6.40
 * And we map 4.x to 2.6.60+x, so 4.0 would be 2.6.60.
 */
static int override_release(char __user *release, size_t len)
{
	int ret = 0;

	if (current->personality & UNAME26) {
		const char *rest = UTS_RELEASE;
		char buf[65] = { 0 };
		int ndots = 0;
		unsigned v;
		size_t copy;

		while (*rest) {
			if (*rest == '.' && ++ndots >= 3)
				break;
			if (!isdigit(*rest) && *rest != '.')
				break;
			rest++;
		}
		v = ((LINUX_VERSION_CODE >> 8) & 0xff) + 60;
		copy = clamp_t(size_t, len, 1, sizeof(buf));
		copy = scnprintf(buf, copy, "2.6.%u%s", v, rest);
		ret = copy_to_user(release, buf, copy + 1);
	}
	return ret;
}

SYSCALL_DEFINE1(newuname, struct new_utsname __user *, name)
{
	int errno = 0;

	down_read(&uts_sem);
	if (copy_to_user(name, utsname(), sizeof *name))
		errno = -EFAULT;
	up_read(&uts_sem);

	if (!errno && override_release(name->release, sizeof(name->release)))
		errno = -EFAULT;
	if (!errno && override_architecture(name))
		errno = -EFAULT;
	return errno;
}

#ifdef __ARCH_WANT_SYS_OLD_UNAME
/*
 * Old cruft
 */
SYSCALL_DEFINE1(uname, struct old_utsname __user *, name)
{
	int error = 0;

	if (!name)
		return -EFAULT;

	down_read(&uts_sem);
	if (copy_to_user(name, utsname(), sizeof(*name)))
		error = -EFAULT;
	up_read(&uts_sem);

	if (!error && override_release(name->release, sizeof(name->release)))
		error = -EFAULT;
	if (!error && override_architecture(name))
		error = -EFAULT;
	return error;
}

SYSCALL_DEFINE1(olduname, struct oldold_utsname __user *, name)
{
	int error;

	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE, name, sizeof(struct oldold_utsname)))
		return -EFAULT;

	down_read(&uts_sem);
	error = __copy_to_user(&name->sysname, &utsname()->sysname,
			       __OLD_UTS_LEN);
	error |= __put_user(0, name->sysname + __OLD_UTS_LEN);
	error |= __copy_to_user(&name->nodename, &utsname()->nodename,
				__OLD_UTS_LEN);
	error |= __put_user(0, name->nodename + __OLD_UTS_LEN);
	error |= __copy_to_user(&name->release, &utsname()->release,
				__OLD_UTS_LEN);
	error |= __put_user(0, name->release + __OLD_UTS_LEN);
	error |= __copy_to_user(&name->version, &utsname()->version,
				__OLD_UTS_LEN);
	error |= __put_user(0, name->version + __OLD_UTS_LEN);
	error |= __copy_to_user(&name->machine, &utsname()->machine,
				__OLD_UTS_LEN);
	error |= __put_user(0, name->machine + __OLD_UTS_LEN);
	up_read(&uts_sem);

	if (!error && override_architecture(name))
		error = -EFAULT;
	if (!error && override_release(name->release, sizeof(name->release)))
		error = -EFAULT;
	return error ? -EFAULT : 0;
}
#endif

SYSCALL_DEFINE2(sethostname, char __user *, name, int, len)
{
	int errno;
	char tmp[__NEW_UTS_LEN];

	if (!ns_capable(current->nsproxy->uts_ns->user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	if (len < 0 || len > __NEW_UTS_LEN)
		return -EINVAL;
	down_write(&uts_sem);
	errno = -EFAULT;
	if (!copy_from_user(tmp, name, len)) {
		struct new_utsname *u = utsname();

		memcpy(u->nodename, tmp, len);
		memset(u->nodename + len, 0, sizeof(u->nodename) - len);
		errno = 0;
		uts_proc_notify(UTS_PROC_HOSTNAME);
	}
	up_write(&uts_sem);
	return errno;
}

#ifdef __ARCH_WANT_SYS_GETHOSTNAME

SYSCALL_DEFINE2(gethostname, char __user *, name, int, len)
{
	int i, errno;
	struct new_utsname *u;

	if (len < 0)
		return -EINVAL;
	down_read(&uts_sem);
	u = utsname();
	i = 1 + strlen(u->nodename);
	if (i > len)
		i = len;
	errno = 0;
	if (copy_to_user(name, u->nodename, i))
		errno = -EFAULT;
	up_read(&uts_sem);
	return errno;
}

#endif

/*
 * Only setdomainname; getdomainname can be implemented by calling
 * uname()
 */
SYSCALL_DEFINE2(setdomainname, char __user *, name, int, len)
{
	int errno;
	char tmp[__NEW_UTS_LEN];

	if (!ns_capable(current->nsproxy->uts_ns->user_ns, CAP_SYS_ADMIN))
		return -EPERM;
	if (len < 0 || len > __NEW_UTS_LEN)
		return -EINVAL;

	down_write(&uts_sem);
	errno = -EFAULT;
	if (!copy_from_user(tmp, name, len)) {
		struct new_utsname *u = utsname();

		memcpy(u->domainname, tmp, len);
		memset(u->domainname + len, 0, sizeof(u->domainname) - len);
		errno = 0;
		uts_proc_notify(UTS_PROC_DOMAINNAME);
	}
	up_write(&uts_sem);
	return errno;
}

SYSCALL_DEFINE2(getrlimit, unsigned int, resource, struct rlimit __user *, rlim)
{
	struct rlimit value;
	int ret;

	ret = do_prlimit(current, resource, NULL, &value);
	if (!ret)
		ret = copy_to_user(rlim, &value, sizeof(*rlim)) ? -EFAULT : 0;

	return ret;
}

#ifdef __ARCH_WANT_SYS_OLD_GETRLIMIT

/*
 *	Back compatibility for getrlimit. Needed for some apps.
 */
SYSCALL_DEFINE2(old_getrlimit, unsigned int, resource,
		struct rlimit __user *, rlim)
{
	struct rlimit x;
	if (resource >= RLIM_NLIMITS)
		return -EINVAL;

	task_lock(current->group_leader);
	x = current->signal->rlim[resource];
	task_unlock(current->group_leader);
	if (x.rlim_cur > 0x7FFFFFFF)
		x.rlim_cur = 0x7FFFFFFF;
	if (x.rlim_max > 0x7FFFFFFF)
		x.rlim_max = 0x7FFFFFFF;
	return copy_to_user(rlim, &x, sizeof(x)) ? -EFAULT : 0;
}

#endif

static inline bool rlim64_is_infinity(__u64 rlim64)
{
#if BITS_PER_LONG < 64
	return rlim64 >= ULONG_MAX;
#else
	return rlim64 == RLIM64_INFINITY;
#endif
}

static void rlim_to_rlim64(const struct rlimit *rlim, struct rlimit64 *rlim64)
{
	if (rlim->rlim_cur == RLIM_INFINITY)
		rlim64->rlim_cur = RLIM64_INFINITY;
	else
		rlim64->rlim_cur = rlim->rlim_cur;
	if (rlim->rlim_max == RLIM_INFINITY)
		rlim64->rlim_max = RLIM64_INFINITY;
	else
		rlim64->rlim_max = rlim->rlim_max;
}

static void rlim64_to_rlim(const struct rlimit64 *rlim64, struct rlimit *rlim)
{
	if (rlim64_is_infinity(rlim64->rlim_cur))
		rlim->rlim_cur = RLIM_INFINITY;
	else
		rlim->rlim_cur = (unsigned long)rlim64->rlim_cur;
	if (rlim64_is_infinity(rlim64->rlim_max))
		rlim->rlim_max = RLIM_INFINITY;
	else
		rlim->rlim_max = (unsigned long)rlim64->rlim_max;
}

/* make sure you are allowed to change @tsk limits before calling this */
int do_prlimit(struct task_struct *tsk, unsigned int resource,
		struct rlimit *new_rlim, struct rlimit *old_rlim)
{
	struct rlimit *rlim;
	int retval = 0;

	if (resource >= RLIM_NLIMITS)
		return -EINVAL;
	if (new_rlim) {
		if (new_rlim->rlim_cur > new_rlim->rlim_max)
			return -EINVAL;
		if (resource == RLIMIT_NOFILE &&
				new_rlim->rlim_max > sysctl_nr_open)
			return -EPERM;
	}

	/* protect tsk->signal and tsk->sighand from disappearing */
	read_lock(&tasklist_lock);
	if (!tsk->sighand) {
		retval = -ESRCH;
		goto out;
	}

	rlim = tsk->signal->rlim + resource;
	task_lock(tsk->group_leader);
	if (new_rlim) {
		/* Keep the capable check against init_user_ns until
		   cgroups can contain all limits */
		if (new_rlim->rlim_max > rlim->rlim_max &&
				!capable(CAP_SYS_RESOURCE))
			retval = -EPERM;
		if (!retval)
			retval = security_task_setrlimit(tsk->group_leader,
					resource, new_rlim);
		if (resource == RLIMIT_CPU && new_rlim->rlim_cur == 0) {
			/*
			 * The caller is asking for an immediate RLIMIT_CPU
			 * expiry.  But we use the zero value to mean "it was
			 * never set".  So let's cheat and make it one second
			 * instead
			 */
			new_rlim->rlim_cur = 1;
		}
	}
	if (!retval) {
		if (old_rlim)
			*old_rlim = *rlim;
		if (new_rlim)
			*rlim = *new_rlim;
	}
	task_unlock(tsk->group_leader);

	/*
	 * RLIMIT_CPU handling.   Note that the kernel fails to return an error
	 * code if it rejected the user's attempt to set RLIMIT_CPU.  This is a
	 * very long-standing error, and fixing it now risks breakage of
	 * applications, so we live with it
	 */
	 if (!retval && new_rlim && resource == RLIMIT_CPU &&
			 new_rlim->rlim_cur != RLIM_INFINITY)
		update_rlimit_cpu(tsk, new_rlim->rlim_cur);
out:
	read_unlock(&tasklist_lock);
	return retval;
}

/* rcu lock must be held */
static int check_prlimit_permission(struct task_struct *task)
{
	const struct cred *cred = current_cred(), *tcred;

	if (current == task)
		return 0;

	tcred = __task_cred(task);
	if (uid_eq(cred->uid, tcred->euid) &&
	    uid_eq(cred->uid, tcred->suid) &&
	    uid_eq(cred->uid, tcred->uid)  &&
	    gid_eq(cred->gid, tcred->egid) &&
	    gid_eq(cred->gid, tcred->sgid) &&
	    gid_eq(cred->gid, tcred->gid))
		return 0;
	if (ns_capable(tcred->user_ns, CAP_SYS_RESOURCE))
		return 0;

	return -EPERM;
}

SYSCALL_DEFINE4(prlimit64, pid_t, pid, unsigned int, resource,
		const struct rlimit64 __user *, new_rlim,
		struct rlimit64 __user *, old_rlim)
{
	struct rlimit64 old64, new64;
	struct rlimit old, new;
	struct task_struct *tsk;
	int ret;

	if (new_rlim) {
		if (copy_from_user(&new64, new_rlim, sizeof(new64)))
			return -EFAULT;
		rlim64_to_rlim(&new64, &new);
	}

	rcu_read_lock();
	tsk = pid ? find_task_by_vpid(pid) : current;
	if (!tsk) {
		rcu_read_unlock();
		return -ESRCH;
	}
	ret = check_prlimit_permission(tsk);
	if (ret) {
		rcu_read_unlock();
		return ret;
	}
	get_task_struct(tsk);
	rcu_read_unlock();

	ret = do_prlimit(tsk, resource, new_rlim ? &new : NULL,
			old_rlim ? &old : NULL);

	if (!ret && old_rlim) {
		rlim_to_rlim64(&old, &old64);
		if (copy_to_user(old_rlim, &old64, sizeof(old64)))
			ret = -EFAULT;
	}

	put_task_struct(tsk);
	return ret;
}

SYSCALL_DEFINE2(setrlimit, unsigned int, resource, struct rlimit __user *, rlim)
{
	struct rlimit new_rlim;

	if (copy_from_user(&new_rlim, rlim, sizeof(*rlim)))
		return -EFAULT;
	return do_prlimit(current, resource, &new_rlim, NULL);
}

/*
 * It would make sense to put struct rusage in the task_struct,
 * except that would make the task_struct be *really big*.  After
 * task_struct gets moved into malloc'ed memory, it would
 * make sense to do this.  It will make moving the rest of the information
 * a lot simpler!  (Which we're not doing right now because we're not
 * measuring them yet).
 *
 * When sampling multiple threads for RUSAGE_SELF, under SMP we might have
 * races with threads incrementing their own counters.  But since word
 * reads are atomic, we either get new values or old values and we don't
 * care which for the sums.  We always take the siglock to protect reading
 * the c* fields from p->signal from races with exit.c updating those
 * fields when reaping, so a sample either gets all the additions of a
 * given child after it's reaped, or none so this sample is before reaping.
 *
 * Locking:
 * We need to take the siglock for CHILDEREN, SELF and BOTH
 * for  the cases current multithreaded, non-current single threaded
 * non-current multithreaded.  Thread traversal is now safe with
 * the siglock held.
 * Strictly speaking, we donot need to take the siglock if we are current and
 * single threaded,  as no one else can take our signal_struct away, no one
 * else can  reap the  children to update signal->c* counters, and no one else
 * can race with the signal-> fields. If we do not take any lock, the
 * signal-> fields could be read out of order while another thread was just
 * exiting. So we should  place a read memory barrier when we avoid the lock.
 * On the writer side,  write memory barrier is implied in  __exit_signal
 * as __exit_signal releases  the siglock spinlock after updating the signal->
 * fields. But we don't do this yet to keep things simple.
 *
 */

static void accumulate_thread_rusage(struct task_struct *t, struct rusage *r)
{
	r->ru_nvcsw += t->nvcsw;
	r->ru_nivcsw += t->nivcsw;
	r->ru_minflt += t->min_flt;
	r->ru_majflt += t->maj_flt;
	r->ru_inblock += task_io_get_inblock(t);
	r->ru_oublock += task_io_get_oublock(t);
}

static void k_getrusage(struct task_struct *p, int who, struct rusage *r)
{
	struct task_struct *t;
	unsigned long flags;
	cputime_t tgutime, tgstime, utime, stime;
	unsigned long maxrss = 0;

	memset((char *)r, 0, sizeof (*r));
	utime = stime = 0;

	if (who == RUSAGE_THREAD) {
		task_cputime_adjusted(current, &utime, &stime);
		accumulate_thread_rusage(p, r);
		maxrss = p->signal->maxrss;
		goto out;
	}

	if (!lock_task_sighand(p, &flags))
		return;

	switch (who) {
	case RUSAGE_BOTH:
	case RUSAGE_CHILDREN:
		utime = p->signal->cutime;
		stime = p->signal->cstime;
		r->ru_nvcsw = p->signal->cnvcsw;
		r->ru_nivcsw = p->signal->cnivcsw;
		r->ru_minflt = p->signal->cmin_flt;
		r->ru_majflt = p->signal->cmaj_flt;
		r->ru_inblock = p->signal->cinblock;
		r->ru_oublock = p->signal->coublock;
		maxrss = p->signal->cmaxrss;

		if (who == RUSAGE_CHILDREN)
			break;

	case RUSAGE_SELF:
		thread_group_cputime_adjusted(p, &tgutime, &tgstime);
		utime += tgutime;
		stime += tgstime;
		r->ru_nvcsw += p->signal->nvcsw;
		r->ru_nivcsw += p->signal->nivcsw;
		r->ru_minflt += p->signal->min_flt;
		r->ru_majflt += p->signal->maj_flt;
		r->ru_inblock += p->signal->inblock;
		r->ru_oublock += p->signal->oublock;
		if (maxrss < p->signal->maxrss)
			maxrss = p->signal->maxrss;
		t = p;
		do {
			accumulate_thread_rusage(t, r);
		} while_each_thread(p, t);
		break;

	default:
		BUG();
	}
	unlock_task_sighand(p, &flags);

out:
	cputime_to_timeval(utime, &r->ru_utime);
	cputime_to_timeval(stime, &r->ru_stime);

	if (who != RUSAGE_CHILDREN) {
		struct mm_struct *mm = get_task_mm(p);

		if (mm) {
			setmax_mm_hiwater_rss(&maxrss, mm);
			mmput(mm);
		}
	}
	r->ru_maxrss = maxrss * (PAGE_SIZE / 1024); /* convert pages to KBs */
}

int getrusage(struct task_struct *p, int who, struct rusage __user *ru)
{
	struct rusage r;

	k_getrusage(p, who, &r);
	return copy_to_user(ru, &r, sizeof(r)) ? -EFAULT : 0;
}

SYSCALL_DEFINE2(getrusage, int, who, struct rusage __user *, ru)
{
	if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN &&
	    who != RUSAGE_THREAD)
		return -EINVAL;
	return getrusage(current, who, ru);
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE2(getrusage, int, who, struct compat_rusage __user *, ru)
{
	struct rusage r;

	if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN &&
	    who != RUSAGE_THREAD)
		return -EINVAL;

	k_getrusage(current, who, &r);
	return put_compat_rusage(&r, ru);
}
#endif

SYSCALL_DEFINE1(umask, int, mask)
{
	mask = xchg(&current->fs->umask, mask & S_IRWXUGO);
	return mask;
}

static int prctl_set_mm_exe_file(struct mm_struct *mm, unsigned int fd)
{
	struct fd exe;
	struct file *old_exe, *exe_file;
	struct inode *inode;
	int err;

	exe = fdget(fd);
	if (!exe.file)
		return -EBADF;

	inode = file_inode(exe.file);

	/*
	 * Because the original mm->exe_file points to executable file, make
	 * sure that this one is executable as well, to avoid breaking an
	 * overall picture.
	 */
	err = -EACCES;
	if (!S_ISREG(inode->i_mode)	||
	    exe.file->f_path.mnt->mnt_flags & MNT_NOEXEC)
		goto exit;

	err = inode_permission(inode, MAY_EXEC);
	if (err)
		goto exit;

	/*
	 * Forbid mm->exe_file change if old file still mapped.
	 */
	exe_file = get_mm_exe_file(mm);
	err = -EBUSY;
	if (exe_file) {
		struct vm_area_struct *vma;

		down_read(&mm->mmap_sem);
		for (vma = mm->mmap; vma; vma = vma->vm_next) {
			if (!vma->vm_file)
				continue;
			if (path_equal(&vma->vm_file->f_path,
				       &exe_file->f_path))
				goto exit_err;
		}

		up_read(&mm->mmap_sem);
		fput(exe_file);
	}

	/*
	 * The symlink can be changed only once, just to disallow arbitrary
	 * transitions malicious software might bring in. This means one
	 * could make a snapshot over all processes running and monitor
	 * /proc/pid/exe changes to notice unusual activity if needed.
	 */
	err = -EPERM;
	if (test_and_set_bit(MMF_EXE_FILE_CHANGED, &mm->flags))
		goto exit;

	err = 0;
	/* set the new file, lockless */
	get_file(exe.file);
	old_exe = xchg(&mm->exe_file, exe.file);
	if (old_exe)
		fput(old_exe);
exit:
	fdput(exe);
	return err;
exit_err:
	up_read(&mm->mmap_sem);
	fput(exe_file);
	goto exit;
}

#ifdef CONFIG_CHECKPOINT_RESTORE
/*
 * WARNING: we don't require any capability here so be very careful
 * in what is allowed for modification from userspace.
 */
static int validate_prctl_map(struct prctl_mm_map *prctl_map)
{
	unsigned long mmap_max_addr = TASK_SIZE;
	struct mm_struct *mm = current->mm;
	int error = -EINVAL, i;

	static const unsigned char offsets[] = {
		offsetof(struct prctl_mm_map, start_code),
		offsetof(struct prctl_mm_map, end_code),
		offsetof(struct prctl_mm_map, start_data),
		offsetof(struct prctl_mm_map, end_data),
		offsetof(struct prctl_mm_map, start_brk),
		offsetof(struct prctl_mm_map, brk),
		offsetof(struct prctl_mm_map, start_stack),
		offsetof(struct prctl_mm_map, arg_start),
		offsetof(struct prctl_mm_map, arg_end),
		offsetof(struct prctl_mm_map, env_start),
		offsetof(struct prctl_mm_map, env_end),
	};

	/*
	 * Make sure the members are not somewhere outside
	 * of allowed address space.
	 */
	for (i = 0; i < ARRAY_SIZE(offsets); i++) {
		u64 val = *(u64 *)((char *)prctl_map + offsets[i]);

		if ((unsigned long)val >= mmap_max_addr ||
		    (unsigned long)val < mmap_min_addr)
			goto out;
	}

	/*
	 * Make sure the pairs are ordered.
	 */
#define __prctl_check_order(__m1, __op, __m2)				\
	((unsigned long)prctl_map->__m1 __op				\
	 (unsigned long)prctl_map->__m2) ? 0 : -EINVAL
	error  = __prctl_check_order(start_code, <, end_code);
	error |= __prctl_check_order(start_data, <, end_data);
	error |= __prctl_check_order(start_brk, <=, brk);
	error |= __prctl_check_order(arg_start, <=, arg_end);
	error |= __prctl_check_order(env_start, <=, env_end);
	if (error)
		goto out;
#undef __prctl_check_order

	error = -EINVAL;

	/*
	 * @brk should be after @end_data in traditional maps.
	 */
	if (prctl_map->start_brk <= prctl_map->end_data ||
	    prctl_map->brk <= prctl_map->end_data)
		goto out;

	/*
	 * Neither we should allow to override limits if they set.
	 */
	if (check_data_rlimit(rlimit(RLIMIT_DATA), prctl_map->brk,
			      prctl_map->start_brk, prctl_map->end_data,
			      prctl_map->start_data))
			goto out;

	/*
	 * Someone is trying to cheat the auxv vector.
	 */
	if (prctl_map->auxv_size) {
		if (!prctl_map->auxv || prctl_map->auxv_size > sizeof(mm->saved_auxv))
			goto out;
	}

	/*
	 * Finally, make sure the caller has the rights to
	 * change /proc/pid/exe link: only local root should
	 * be allowed to.
	 */
	if (prctl_map->exe_fd != (u32)-1) {
		struct user_namespace *ns = current_user_ns();
		const struct cred *cred = current_cred();

		if (!uid_eq(cred->uid, make_kuid(ns, 0)) ||
		    !gid_eq(cred->gid, make_kgid(ns, 0)))
			goto out;
	}

	error = 0;
out:
	return error;
}

static int prctl_set_mm_map(int opt, const void __user *addr, unsigned long data_size)
{
	struct prctl_mm_map prctl_map = { .exe_fd = (u32)-1, };
	unsigned long user_auxv[AT_VECTOR_SIZE];
	struct mm_struct *mm = current->mm;
	int error;

	BUILD_BUG_ON(sizeof(user_auxv) != sizeof(mm->saved_auxv));
	BUILD_BUG_ON(sizeof(struct prctl_mm_map) > 256);

	if (opt == PR_SET_MM_MAP_SIZE)
		return put_user((unsigned int)sizeof(prctl_map),
				(unsigned int __user *)addr);

	if (data_size != sizeof(prctl_map))
		return -EINVAL;

	if (copy_from_user(&prctl_map, addr, sizeof(prctl_map)))
		return -EFAULT;

	error = validate_prctl_map(&prctl_map);
	if (error)
		return error;

	if (prctl_map.auxv_size) {
		memset(user_auxv, 0, sizeof(user_auxv));
		if (copy_from_user(user_auxv,
				   (const void __user *)prctl_map.auxv,
				   prctl_map.auxv_size))
			return -EFAULT;

		/* Last entry must be AT_NULL as specification requires */
		user_auxv[AT_VECTOR_SIZE - 2] = AT_NULL;
		user_auxv[AT_VECTOR_SIZE - 1] = AT_NULL;
	}

	if (prctl_map.exe_fd != (u32)-1)
		error = prctl_set_mm_exe_file(mm, prctl_map.exe_fd);
	down_read(&mm->mmap_sem);
	if (error)
		goto out;

	/*
	 * We don't validate if these members are pointing to
	 * real present VMAs because application may have correspond
	 * VMAs already unmapped and kernel uses these members for statistics
	 * output in procfs mostly, except
	 *
	 *  - @start_brk/@brk which are used in do_brk but kernel lookups
	 *    for VMAs when updating these memvers so anything wrong written
	 *    here cause kernel to swear at userspace program but won't lead
	 *    to any problem in kernel itself
	 */

	mm->start_code	= prctl_map.start_code;
	mm->end_code	= prctl_map.end_code;
	mm->start_data	= prctl_map.start_data;
	mm->end_data	= prctl_map.end_data;
	mm->start_brk	= prctl_map.start_brk;
	mm->brk		= prctl_map.brk;
	mm->start_stack	= prctl_map.start_stack;
	mm->arg_start	= prctl_map.arg_start;
	mm->arg_end	= prctl_map.arg_end;
	mm->env_start	= prctl_map.env_start;
	mm->env_end	= prctl_map.env_end;

	/*
	 * Note this update of @saved_auxv is lockless thus
	 * if someone reads this member in procfs while we're
	 * updating -- it may get partly updated results. It's
	 * known and acceptable trade off: we leave it as is to
	 * not introduce additional locks here making the kernel
	 * more complex.
	 */
	if (prctl_map.auxv_size)
		memcpy(mm->saved_auxv, user_auxv, sizeof(user_auxv));

	error = 0;
out:
	up_read(&mm->mmap_sem);
	return error;
}
#endif /* CONFIG_CHECKPOINT_RESTORE */

static int prctl_set_mm(int opt, unsigned long addr,
			unsigned long arg4, unsigned long arg5)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int error;

	if (arg5 || (arg4 && (opt != PR_SET_MM_AUXV &&
			      opt != PR_SET_MM_MAP &&
			      opt != PR_SET_MM_MAP_SIZE)))
		return -EINVAL;

#ifdef CONFIG_CHECKPOINT_RESTORE
	if (opt == PR_SET_MM_MAP || opt == PR_SET_MM_MAP_SIZE)
		return prctl_set_mm_map(opt, (const void __user *)addr, arg4);
#endif

	if (!capable(CAP_SYS_RESOURCE))
		return -EPERM;

	if (opt == PR_SET_MM_EXE_FILE)
		return prctl_set_mm_exe_file(mm, (unsigned int)addr);

	if (addr >= TASK_SIZE || addr < mmap_min_addr)
		return -EINVAL;

	error = -EINVAL;

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, addr);

	switch (opt) {
	case PR_SET_MM_START_CODE:
		mm->start_code = addr;
		break;
	case PR_SET_MM_END_CODE:
		mm->end_code = addr;
		break;
	case PR_SET_MM_START_DATA:
		mm->start_data = addr;
		break;
	case PR_SET_MM_END_DATA:
		mm->end_data = addr;
		break;

	case PR_SET_MM_START_BRK:
		if (addr <= mm->end_data)
			goto out;

		if (check_data_rlimit(rlimit(RLIMIT_DATA), mm->brk, addr,
				      mm->end_data, mm->start_data))
			goto out;

		mm->start_brk = addr;
		break;

	case PR_SET_MM_BRK:
		if (addr <= mm->end_data)
			goto out;

		if (check_data_rlimit(rlimit(RLIMIT_DATA), addr, mm->start_brk,
				      mm->end_data, mm->start_data))
			goto out;

		mm->brk = addr;
		break;

	/*
	 * If command line arguments and environment
	 * are placed somewhere else on stack, we can
	 * set them up here, ARG_START/END to setup
	 * command line argumets and ENV_START/END
	 * for environment.
	 */
	case PR_SET_MM_START_STACK:
	case PR_SET_MM_ARG_START:
	case PR_SET_MM_ARG_END:
	case PR_SET_MM_ENV_START:
	case PR_SET_MM_ENV_END:
		if (!vma) {
			error = -EFAULT;
			goto out;
		}
		if (opt == PR_SET_MM_START_STACK)
			mm->start_stack = addr;
		else if (opt == PR_SET_MM_ARG_START)
			mm->arg_start = addr;
		else if (opt == PR_SET_MM_ARG_END)
			mm->arg_end = addr;
		else if (opt == PR_SET_MM_ENV_START)
			mm->env_start = addr;
		else if (opt == PR_SET_MM_ENV_END)
			mm->env_end = addr;
		break;

	/*
	 * This doesn't move auxiliary vector itself
	 * since it's pinned to mm_struct, but allow
	 * to fill vector with new values. It's up
	 * to a caller to provide sane values here
	 * otherwise user space tools which use this
	 * vector might be unhappy.
	 */
	case PR_SET_MM_AUXV: {
		unsigned long user_auxv[AT_VECTOR_SIZE];

		if (arg4 > sizeof(user_auxv))
			goto out;
		up_read(&mm->mmap_sem);

		if (copy_from_user(user_auxv, (const void __user *)addr, arg4))
			return -EFAULT;

		/* Make sure the last entry is always AT_NULL */
		user_auxv[AT_VECTOR_SIZE - 2] = 0;
		user_auxv[AT_VECTOR_SIZE - 1] = 0;

		BUILD_BUG_ON(sizeof(user_auxv) != sizeof(mm->saved_auxv));

		task_lock(current);
		memcpy(mm->saved_auxv, user_auxv, arg4);
		task_unlock(current);

		return 0;
	}
	default:
		goto out;
	}

	error = 0;
out:
	up_read(&mm->mmap_sem);
	return error;
}

#ifdef CONFIG_CHECKPOINT_RESTORE
static int prctl_get_tid_address(struct task_struct *me, int __user **tid_addr)
{
	return put_user(me->clear_child_tid, tid_addr);
}
#else
static int prctl_get_tid_address(struct task_struct *me, int __user **tid_addr)
{
	return -EINVAL;
}
#endif

SYSCALL_DEFINE5(prctl, int, option, unsigned long, arg2, unsigned long, arg3,
		unsigned long, arg4, unsigned long, arg5)
{
	struct task_struct *me = current;
	unsigned char comm[sizeof(me->comm)];
	long error;

	error = security_task_prctl(option, arg2, arg3, arg4, arg5);
	if (error != -ENOSYS)
		return error;

	error = 0;
	switch (option) {
	case PR_SET_PDEATHSIG:
		if (!valid_signal(arg2)) {
			error = -EINVAL;
			break;
		}
		me->pdeath_signal = arg2;
		break;
	case PR_GET_PDEATHSIG:
		error = put_user(me->pdeath_signal, (int __user *)arg2);
		break;
	case PR_GET_DUMPABLE:
		error = get_dumpable(me->mm);
		break;
	case PR_SET_DUMPABLE:
		if (arg2 != SUID_DUMP_DISABLE && arg2 != SUID_DUMP_USER) {
			error = -EINVAL;
			break;
		}
		set_dumpable(me->mm, arg2);
		break;

	case PR_SET_UNALIGN:
		error = SET_UNALIGN_CTL(me, arg2);
		break;
	case PR_GET_UNALIGN:
		error = GET_UNALIGN_CTL(me, arg2);
		break;
	case PR_SET_FPEMU:
		error = SET_FPEMU_CTL(me, arg2);
		break;
	case PR_GET_FPEMU:
		error = GET_FPEMU_CTL(me, arg2);
		break;
	case PR_SET_FPEXC:
		error = SET_FPEXC_CTL(me, arg2);
		break;
	case PR_GET_FPEXC:
		error = GET_FPEXC_CTL(me, arg2);
		break;
	case PR_GET_TIMING:
		error = PR_TIMING_STATISTICAL;
		break;
	case PR_SET_TIMING:
		if (arg2 != PR_TIMING_STATISTICAL)
			error = -EINVAL;
		break;
	case PR_SET_NAME:
		comm[sizeof(me->comm) - 1] = 0;
		if (strncpy_from_user(comm, (char __user *)arg2,
				      sizeof(me->comm) - 1) < 0)
			return -EFAULT;
		set_task_comm(me, comm);
		proc_comm_connector(me);
		break;
	case PR_GET_NAME:
		get_task_comm(comm, me);
		if (copy_to_user((char __user *)arg2, comm, sizeof(comm)))
			return -EFAULT;
		break;
	case PR_GET_ENDIAN:
		error = GET_ENDIAN(me, arg2);
		break;
	case PR_SET_ENDIAN:
		error = SET_ENDIAN(me, arg2);
		break;
	case PR_GET_SECCOMP:
		error = prctl_get_seccomp();
		break;
	case PR_SET_SECCOMP:
		error = prctl_set_seccomp(arg2, (char __user *)arg3);
		break;
	case PR_GET_TSC:
		error = GET_TSC_CTL(arg2);
		break;
	case PR_SET_TSC:
		error = SET_TSC_CTL(arg2);
		break;
	case PR_TASK_PERF_EVENTS_DISABLE:
		error = perf_event_task_disable();
		break;
	case PR_TASK_PERF_EVENTS_ENABLE:
		error = perf_event_task_enable();
		break;
	case PR_GET_TIMERSLACK:
		error = current->timer_slack_ns;
		break;
	case PR_SET_TIMERSLACK:
		if (arg2 <= 0)
			current->timer_slack_ns =
					current->default_timer_slack_ns;
		else
			current->timer_slack_ns = arg2;
		break;
	case PR_MCE_KILL:
		if (arg4 | arg5)
			return -EINVAL;
		switch (arg2) {
		case PR_MCE_KILL_CLEAR:
			if (arg3 != 0)
				return -EINVAL;
			current->flags &= ~PF_MCE_PROCESS;
			break;
		case PR_MCE_KILL_SET:
			current->flags |= PF_MCE_PROCESS;
			if (arg3 == PR_MCE_KILL_EARLY)
				current->flags |= PF_MCE_EARLY;
			else if (arg3 == PR_MCE_KILL_LATE)
				current->flags &= ~PF_MCE_EARLY;
			else if (arg3 == PR_MCE_KILL_DEFAULT)
				current->flags &=
						~(PF_MCE_EARLY|PF_MCE_PROCESS);
			else
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
		break;
	case PR_MCE_KILL_GET:
		if (arg2 | arg3 | arg4 | arg5)
			return -EINVAL;
		if (current->flags & PF_MCE_PROCESS)
			error = (current->flags & PF_MCE_EARLY) ?
				PR_MCE_KILL_EARLY : PR_MCE_KILL_LATE;
		else
			error = PR_MCE_KILL_DEFAULT;
		break;
	case PR_SET_MM:
		error = prctl_set_mm(arg2, arg3, arg4, arg5);
		break;
	case PR_GET_TID_ADDRESS:
		error = prctl_get_tid_address(me, (int __user **)arg2);
		break;
	case PR_SET_CHILD_SUBREAPER:
		me->signal->is_child_subreaper = !!arg2;
		break;
	case PR_GET_CHILD_SUBREAPER:
		error = put_user(me->signal->is_child_subreaper,
				 (int __user *)arg2);
		break;
	case PR_SET_NO_NEW_PRIVS:
		if (arg2 != 1 || arg3 || arg4 || arg5)
			return -EINVAL;

		task_set_no_new_privs(current);
		break;
	case PR_GET_NO_NEW_PRIVS:
		if (arg2 || arg3 || arg4 || arg5)
			return -EINVAL;
		return task_no_new_privs(current) ? 1 : 0;
	case PR_GET_THP_DISABLE:
		if (arg2 || arg3 || arg4 || arg5)
			return -EINVAL;
		error = !!(me->mm->def_flags & VM_NOHUGEPAGE);
		break;
	case PR_SET_THP_DISABLE:
		if (arg3 || arg4 || arg5)
			return -EINVAL;
		down_write(&me->mm->mmap_sem);
		if (arg2)
			me->mm->def_flags |= VM_NOHUGEPAGE;
		else
			me->mm->def_flags &= ~VM_NOHUGEPAGE;
		up_write(&me->mm->mmap_sem);
		break;
	case PR_MPX_ENABLE_MANAGEMENT:
		if (arg2 || arg3 || arg4 || arg5)
			return -EINVAL;
		error = MPX_ENABLE_MANAGEMENT(me);
		break;
	case PR_MPX_DISABLE_MANAGEMENT:
		if (arg2 || arg3 || arg4 || arg5)
			return -EINVAL;
		error = MPX_DISABLE_MANAGEMENT(me);
		break;
	case PR_SET_FP_MODE:
		error = SET_FP_MODE(me, arg2);
		break;
	case PR_GET_FP_MODE:
		error = GET_FP_MODE(me);
		break;
	default:
		error = -EINVAL;
		break;
	}
	return error;
}

SYSCALL_DEFINE3(getcpu, unsigned __user *, cpup, unsigned __user *, nodep,
		struct getcpu_cache __user *, unused)
{
	int err = 0;
	int cpu = raw_smp_processor_id();

	if (cpup)
		err |= put_user(cpu, cpup);
	if (nodep)
		err |= put_user(cpu_to_node(cpu), nodep);
	return err ? -EFAULT : 0;
}

/**
 * do_sysinfo - fill in sysinfo struct
 * @info: pointer to buffer to fill
 */
static int do_sysinfo(struct sysinfo *info)
{
	unsigned long mem_total, sav_total;
	unsigned int mem_unit, bitcount;
	struct timespec tp;

	memset(info, 0, sizeof(struct sysinfo));

	get_monotonic_boottime(&tp);
	info->uptime = tp.tv_sec + (tp.tv_nsec ? 1 : 0);

	get_avenrun(info->loads, 0, SI_LOAD_SHIFT - FSHIFT);

	info->procs = nr_threads;

	si_meminfo(info);
	si_swapinfo(info);

	/*
	 * If the sum of all the available memory (i.e. ram + swap)
	 * is less than can be stored in a 32 bit unsigned long then
	 * we can be binary compatible with 2.2.x kernels.  If not,
	 * well, in that case 2.2.x was broken anyways...
	 *
	 *  -Erik Andersen <andersee@debian.org>
	 */

	mem_total = info->totalram + info->totalswap;
	if (mem_total < info->totalram || mem_total < info->totalswap)
		goto out;
	bitcount = 0;
	mem_unit = info->mem_unit;
	while (mem_unit > 1) {
		bitcount++;
		mem_unit >>= 1;
		sav_total = mem_total;
		mem_total <<= 1;
		if (mem_total < sav_total)
			goto out;
	}

	/*
	 * If mem_total did not overflow, multiply all memory values by
	 * info->mem_unit and set it to 1.  This leaves things compatible
	 * with 2.2.x, and also retains compatibility with earlier 2.4.x
	 * kernels...
	 */

	info->mem_unit = 1;
	info->totalram <<= bitcount;
	info->freeram <<= bitcount;
	info->sharedram <<= bitcount;
	info->bufferram <<= bitcount;
	info->totalswap <<= bitcount;
	info->freeswap <<= bitcount;
	info->totalhigh <<= bitcount;
	info->freehigh <<= bitcount;

out:
	return 0;
}

SYSCALL_DEFINE1(sysinfo, struct sysinfo __user *, info)
{
	struct sysinfo val;

	do_sysinfo(&val);

	if (copy_to_user(info, &val, sizeof(struct sysinfo)))
		return -EFAULT;

	return 0;
}

#ifdef CONFIG_COMPAT
struct compat_sysinfo {
	s32 uptime;
	u32 loads[3];
	u32 totalram;
	u32 freeram;
	u32 sharedram;
	u32 bufferram;
	u32 totalswap;
	u32 freeswap;
	u16 procs;
	u16 pad;
	u32 totalhigh;
	u32 freehigh;
	u32 mem_unit;
	char _f[20-2*sizeof(u32)-sizeof(int)];
};

COMPAT_SYSCALL_DEFINE1(sysinfo, struct compat_sysinfo __user *, info)
{
	struct sysinfo s;

	do_sysinfo(&s);

	/* Check to see if any memory value is too large for 32-bit and scale
	 *  down if needed
	 */
	if (upper_32_bits(s.totalram) || upper_32_bits(s.totalswap)) {
		int bitcount = 0;

		while (s.mem_unit < PAGE_SIZE) {
			s.mem_unit <<= 1;
			bitcount++;
		}

		s.totalram >>= bitcount;
		s.freeram >>= bitcount;
		s.sharedram >>= bitcount;
		s.bufferram >>= bitcount;
		s.totalswap >>= bitcount;
		s.freeswap >>= bitcount;
		s.totalhigh >>= bitcount;
		s.freehigh >>= bitcount;
	}

	if (!access_ok(VERIFY_WRITE, info, sizeof(struct compat_sysinfo)) ||
	    __put_user(s.uptime, &info->uptime) ||
	    __put_user(s.loads[0], &info->loads[0]) ||
	    __put_user(s.loads[1], &info->loads[1]) ||
	    __put_user(s.loads[2], &info->loads[2]) ||
	    __put_user(s.totalram, &info->totalram) ||
	    __put_user(s.freeram, &info->freeram) ||
	    __put_user(s.sharedram, &info->sharedram) ||
	    __put_user(s.bufferram, &info->bufferram) ||
	    __put_user(s.totalswap, &info->totalswap) ||
	    __put_user(s.freeswap, &info->freeswap) ||
	    __put_user(s.procs, &info->procs) ||
	    __put_user(s.totalhigh, &info->totalhigh) ||
	    __put_user(s.freehigh, &info->freehigh) ||
	    __put_user(s.mem_unit, &info->mem_unit))
		return -EFAULT;

	return 0;
}
#endif /* CONFIG_COMPAT */
