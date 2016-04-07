README
======

Author
------
*Pierre-Luc Manteaux*
*pierre-luc.manteaux@inria.fr*

Licence
-------
This project is licensed under the terms of the GNU GPL v3.0 license.
See Licence file

How to compile/run examples
------------------------------------
    cd examples
    mkdir build
    cd build
    cmake ..
    make
    ./dambreak

How to compile/run tests
--------------------------
    cd tests
    mkdir build
    cd build
    cmake ..
    make test

How to compile the library
----------------------------
    cd hokusai
    mkdir build
    cmake ..
    make

How to generate the documentation
-----------------------------------
    cd hokusai
    mkdir build
    cmake ..
    make doc

To do...soon...
---------------

1. Add more easy primitive shape for fluid 
--1.cube
--3.sphere
--3.cylinder
--4.cone
--5.pyramid
--6.capsule
--7.torus
3. Output vector field image
4. Parameters preset
5. Smoke simulation
6. Add profiling functions to get an idea of the requested computational time.
7. Add exporting tools to standard format such as Disney Particle Format

Done
----

1. Basic primitives sampling
1. Arbitrary boundaries handlling
