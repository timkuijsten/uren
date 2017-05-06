# uren

uren is a project time tracking tool with stopwatch support.

Features:
* Simple project based time tracking
* vi-like key bindings for fast navigation
* Simple and robust data layer, one text file per entry so easy to backup
* Tab-completion of project names

Status: **beta**, make sure you have backups of ~/.uren

uren is primarily developed on OS X 10.11 and tested on Debian and Ubuntu.

![Screenshot of uren](https://netsend.nl/uren/uren.png)


## Installation

Compile and install uren:

```sh
$ git clone https://github.com/timkuijsten/uren.git
$ cd uren
$ make
$ sudo make install
```


### Build requirements

* C compiler (with reasonable C99 support)
* BSD or GNU make
* ncurses
* Berkeley DB 1.8x


## Documentation

For documentation please refer to the [manpage].


## License

ISC

Copyright (c) 2017 Tim Kuijsten

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

---

[manpage]: https://netsend.nl/uren/uren.1.html
