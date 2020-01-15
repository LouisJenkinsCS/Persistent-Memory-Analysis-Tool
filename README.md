Valgrind w/ Persistent Memory Analysis Tool (PMAT)
======================================

[![Build Status](https://travis-ci.com/LouisJenkinsCS/valgrind.svg?branch=pmem-3.14)](https://travis-ci.com/louisjenkinscs/valgrind)

**This Product has been derived from Intel's ['pmemcheck'](https://github.com/pmem/valgrind) tool.**

This is the top-level README.md the enhanced version on Valgrind.
This version has support for the new CLFLUSHOPT and CLWB instructions.
Introduced is the Persistent Memory Analysis Tool (PMAT), which is a valgrind plugin
capable of performing online sampling and verification of user programs with performance
that provides equal amounts of coverage and far superior performance to that of Intel's pmemcheck + pmreorder. 
This tool is still in development, and is currently in its early 'alpha' phase.

Please see the file COPYING for information on the license.

The layout is identical to the original Valgrind.
The new tool is available in:

* **PMAT** -- An Online and Sampling **P**ersistent **M**emory **A**nalysis **T**ool

All packages necessary to build this modified version of Valgrind are
the same as for the original version.

Once the build system is setup, Valgrind is built using
these command at the top level:
```
	$ ./autogen.sh
	$ ./configure [--prefix=/where/to/install]
	$ make
	$ make install
```

**TODO:** Tests do not run PMAT tests yet, please see pmat/tests for how they should be run.

For more information on Valgrind please refer to the original README
files and the documentation which is available at:
```
	$PREFIX/share/doc/valgrind/manual.html
```
Where $PREFIX is the path specified with --prefix to configure.

For information on how to run the new tool refer to the appropriate
part of the documentation or type:
```
	$ valgrind --tool=pmat --help
```

For more information on the modifications made to Valgrind
contact Louis Jenkins (LouisJenkinsCS@hotmail.com).
