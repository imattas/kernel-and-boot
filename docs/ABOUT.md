# About `os`

`os` is a from-scratch x86_64 operating system intended to explore the complete path from firmware entry to a native kernel and eventually a native userland.

The project deliberately avoids the structure of a tiny educational kernel. Its kernel is modular monolithic, with deep subsystem separation for architecture support, memory management, scheduling, filesystems, devices, drivers, execution, system calls, timekeeping, debugging, and security. Components may share kernel address space while still exposing deliberate internal interfaces.

The bootloader is part of the operating system architecture. UEFI is the primary implementation target because it provides the modern x86_64 firmware environment. Legacy BIOS support is reserved as a later independent boot path that converges on the same kernel boot protocol.

C is the main implementation language. NASM assembly may be used extensively for architectural entry points, processor state, interrupt and exception stubs, context switching, syscall transitions, SMP startup, and other operations where assembly provides better control.


Build products are deliberately isolated: `build/` owns all generated and intermediate artifacts, while `dist/` contains only final distributable images such as `os.img`. Source trees remain artifact-free.
