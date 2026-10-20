CP/M originally stood for Control Program/Monitor. Later, CP/M became known as Control Program for Microcomputers. It was a mass-market operating system created in 1974 for Intel 8080/85-based microcomputers by Gary Kildall of Digital Research, Inc.

The first version was single-tasking on 8-bit processors with support for up to 64 kilobytes of memory. Later versions of CP/M added multiple-user variations and were migrated to 16-bit processors.

For more information about CP/M, see the [CP/M Wikipedia article](https://en.wikipedia.org/wiki/CP/M?azure-portal=true){:target=_blank} and [CP/M Frequently Asked Questions](http://www.gaby.de/faq.htm){:target=_blank}.

## Digital Research CP/M Operating System Manual

You will find a wealth of information about CP/M, including compilers, assemblers, debuggers, and more in the [Digital Research CP/M Operating System Manual](http://www.gaby.de/cpm/manuals/archive/cpm22htm/){:target=_blank}

## Programming the Altair emulator

From CP/M, you can program the Altair emulator using Microsoft BASIC, BD Software C, the Intel Assembler and Linker, and the Microsoft MACRO-80 Assembler.

## Get started with CP/M

Here are some CP/M commands to get you started:

- Changing drives. The Altair emulator mounts two drives, drive A and drive B. To change drives, from the Altair CP/M command prompt in the Web Terminal, type the drive name, then press the Enter key.

    ```cpm
    a:

    b:
    ```

- Display a directory listing.

    ```cpm
    dir

    dir *.BAS

    ls
    ```

- Erase a file.

    ```cpm
    era *.txt
    ```

- Copy Microsoft BASIC `mbasic.com` from drive A to drive B.

    ```cpm
    a:pip b:mbasic.com.com=a:mbasic.com
    ```

- List the contents of a file.

    ```cpm
    type lunar.bas
    ```

- Rename a file.

    ```cpm
    ren lander.bas=lunar.bas
    ```

## Get started with retro gaming

## Download a retro game

The `Apps/GAMES` folder contains classic BASIC games. The local/Docker
FT service can copy them from the configured Apps root into CP/M.
On ESP32, make them available through the configured remote FT server.
The obsolete GF HTTP downloader has been retired.

### Using the Built-in Games Repository

Use the current FT client's text-transfer option:

1. From the CP/M command prompt:

    ```cpm
    ft -g games/awari.bas
    ```

2. The game file will be downloaded directly to your CP/M filesystem.

3. Start the game from the CP/M command line:

    ```cpm
    mbasic awari
    ```

### Common Games Available

Here are some popular games you can download:

```cpm
ft -g games/awari.bas
ft -g games/bagels.bas
ft -g games/animal.bas
```

### Alternative: Manual Repository Access

You can also browse and download games manually from the [Retro Games](https://github.com/AzureSphereCloudEnabledAltair8800/RetroGames){:target=_blank} repository if you prefer to see what's available before downloading.

If a game requires additional files, make those available through the same
FT server and transfer them too.

## Retro game acknowledgments

This list of games was made possible by the dedicated work of [CP/M Games](http://www.retroarchive.org/cpm/games/games.htm){:target=_blank} and [Vintage BASIC](http://www.vintage-basic.net/games.html){:target=_blank}.
