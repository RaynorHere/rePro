/**
 * shush_vm.h  --  shushCopy VM lifecycle API  (Windows only)
 *
 * Runs user-supplied shell commands to start/stop a virtual machine and
 * polls the SSH port to confirm the desired state.  Any hypervisor whose
 * start and stop operations can be expressed as a single shell command
 * will work (VirtualBox, QEMU, Hyper-V, etc.).
 *
 * Return value convention (different from the SSH/SCP API):
 *   +1  operation took effect  (VM launched / VM stopped)
 *    0  no-op: VM already running (spawn) or already stopped (kill)
 *  < 0  shush_err_t error code
 */

#ifndef SHUSH_VM_H
#define SHUSH_VM_H

#include "shush_copy.h"   /* for shush_err_t */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32

/**
 * Start a VM by running vm_start_cmd and block until SSH is ready on
 * 127.0.0.1:host_port.
 *
 * vm_start_cmd  Shell command to start the VM.  Passed to system(); must
 *               exit after submitting the start request (the VM itself
 *               can boot asynchronously).  Example:
 *               "VBoxManage startvm MyVM --type headless"
 * host_port     SSH port on 127.0.0.1 to poll (pass 9998 for the default).
 * timeout_ms    Maximum wait for SSH in ms; 0 = no limit (pass 10000).
 *
 * Returns  1  VM started and SSH is ready.
 *          0  SSH was already responding; nothing was changed.
 *         <0  shush_err_t failure code (message printed to stderr).
 */
int shush_spawn_vm(const char *vm_start_cmd, int host_port, int timeout_ms);

/**
 * Stop a VM by running vm_stop_cmd and block until host_port closes.
 *
 * vm_stop_cmd  Shell command to stop the VM.  Example:
 *              "VBoxManage controlvm MyVM poweroff"
 * host_port    SSH port on 127.0.0.1 to monitor.
 *
 * Returns  1  VM was stopped and port is closed.
 *          0  Port was not open; nothing was running.
 *         <0  shush_err_t failure code (message printed to stderr).
 */
int shush_kill_vm(const char *vm_stop_cmd, int host_port);

#endif /* _WIN32 */

#ifdef __cplusplus
}
#endif

#endif /* SHUSH_VM_H */
