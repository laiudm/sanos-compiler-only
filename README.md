Tcc compiler - as ported to Sanos
=================================

This is a locally running copy of tcc (called cc here after sanos) that generates 32 bit x86 .exe files. The source is from the [sanos](http://www.jbox.dk/sanos/index.htm) project ( [github] (https://github.com/ringgaard/sanos))

All code is written to use sanos's operating system interface (API [here](http://www.jbox.dk/sanos/index.htm). It therefore requires the "sanos on windows" interface provided by os.dll, which needs to reside with any .exe that's generated.

I've ported over a number of compiler tests from the 8cc compiler project.

1 June 2019
-----------

Completed enhancing the compiler to allow declarations within a for() loop.



