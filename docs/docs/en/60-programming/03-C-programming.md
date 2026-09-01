# C Programming

Altair applications can be built with either the included BDS C compiler or
the host-based dcc compiler. **dcc is strongly recommended for new C
applications.** BDS C remains useful when you want an authentic compiler that
runs entirely inside CP/M.

## Recommended: dcc C Compiler

[dcc](https://davidly.github.io/dcc/) is an open source C compiler for CP/M 2.2
on the Z80. It supports C89 plus CP/M-relevant C99 and C11 features. The
compiler runs on Windows, macOS, and Linux, producing M80 assembly that is
assembled and linked into a CP/M `.COM` application.

Use the [dcc documentation](https://davidly.github.io/dcc/) for the language,
runtime library, limitations, and debugging reference. Start with
[Setting up the toolchain](https://davidly.github.io/dcc/00-setup-toolchain/)
and [Building and linking](https://davidly.github.io/dcc/02-build-and-link/).

Some applications use `dccmake` to compile C on the host and then install the
generated `.COM` file into a CP/M disk image. This workflow requires:

- PowerShell 7 (`pwsh`).
- The dcc toolchain, including `dccmake`.
- [cpmtools](https://www.moria.de/~michael/cpmtools/), with `cpmrm` and
    `cpmcp` available on `PATH`.

See [Build and Install CP/M Apps](../90-appendices/10-build-and-install-cpm-apps.md)
for installation checks, the complete build flow, and details of the MITS
Altair disk format used by the project.

## BDS C Compiler

The BD Software C compiler is included on drive B:. It runs under CP/M and generates
code for the Intel 8080 and Zilog Z80 processors. See the
[BDS C Wikipedia article](https://en.wikipedia.org/wiki/BDS_C){:target=_blank}
for its history.

## BDS C User's Guide

Refer to the [BDS C User's Guide](https://github.com/AzureSphereCloudEnabledAltair8800/Altair8800.manuals/blob/master/BDS_C_Compiler.pdf){:target=_blank} for more about the language and its implementation.

!!! warning "BDS C Symbol Length Limitations"

    BDS C is an early implementation of the C programming language. The biggest **gotcha** is that all identifiers/symbols are unique to 7 characters. For example, the following two variables are treated as the same symbol:

    ```c
    int variable1;
    int variable2;
    ``` 

    **Both are treated as `variabl`. So, be careful when naming variables, functions, and other symbols as you get no compiler warnings or errors and your application will behave unexpectedly.**

!!! warning "BDS C Creating and Initializing Variables"

    BDS C does not support creating and initializing global variables in one step. For example, the following code will not compile:

    ```c
    int count = 0; // This will not compile
    ```

    Instead, you must create and initialize global variables in two steps:

    ```c
    int count; // Create the variable

    main()
    {
        count = 0; // Initialize the variable
        ...
    }
    ```

## Compile C applications

The CP/M disk image includes a simple *HW.C* (Hello, world) application. BDS C language has support for Intel 8080 CPU input and output port instructions. The *HW.C* application displays the system tick count, UTC, and local date and time, and then sleeps for 1 second. For more information about Intel 8080 IO port mappings, refer to [Intel 8080 input and output ports](https://github.com/gloveboxes/Altair8800.Emulator.UN-X/wiki#intel-8080-input-and-output-ports){:target=_blank}.

Follow these steps to list, compile, link, and run the *HW.C* file:

1. List the *hw.c* file

    ```cpm
    type hw.c
    ```

    ```c
    /* Copyright (c) Microsoft Corporation. All rights reserved.
       Licensed under the MIT License. */

    /* C application to demonstrate use of Intel 8080 IO Ports */

    main()
    {
        unsigned c, l;
        char buffer[50];

        printf("\nHello from the Altair 8800 emulator\n\n");

        for (c = 0; c < 65535; c++)
        {
            printf("Count:%u\n", c);
            printf("System tick count: %s\n", get_port_data(41, buffer, 50));
            printf("UTC date and time: %s\n", get_port_data(42, buffer, 50));
            printf("Local date and time: %s\n\n", get_port_data(43, buffer, 50));

            sleep(1); /* Sleep for 1 second */
        }
    }

    /* Sleep for n seconds */
    sleep(seconds)
    char seconds;
    {
        outp(30, seconds); /* Enable sleep for N seconds */
        while (inp(30)); /* Wait for sleep to expire */
    }

    /* Get data from Intel 8080 IO port */
    char *get_port_data(port_num, buffer, buffer_len)
    int port_num;
    char *buffer;
    int buffer_len;
    {
        char ch;
        int index;

        index = 0;

        while ((ch = inp(port_num)) && index < buffer_len) {
            buffer[index++] = ch;
        }
        buffer[index] = 0x00;

        return buffer;
    }
    ```

1. Compile the *hw.c* file:

    ```cpm
    cc hw
    ```

1. Link the *hw* application:

    ```cpm
    clink hw
    ```

1. Run the *hw* application:

    ```cpm
    hw
    ```

1. Stop the *hw* application by selecting **Ctrl+C**.

## Editing files

See [Editing Files](01-Editing-files.md){:target=_blank} for details about transferring, editing, compiling, and running C source files.
