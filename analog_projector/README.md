# [Siggraph Asia 2025] LookUp3D: Data-Driven 3D Scanning

<strong>Giancarlo Pereira<sup>2</sup>, Yidan Gao<sup>1</sup>, Yurii Piadyk<sup>2</sup>, David Fouhey<sup>1,2</sup>, Claudio T. Silva<sup>2,3</sup>, Daniele Panozzo<sup>1</sup></strong>

<small><sup>1</sup>New York University, Courant Institute of Mathematical Sciences</small>

<small><sup>2</sup>New York University, Tandon School of Engineering</small>

<small><sup>3</sup>New York University, Center for Data Science</small>


This folder contains the firmware code to control the analog projector and the CAD model of the analog projector for reproducibility.

Known issues: 
1. moving the linear stage while scanning clogs the process; the firmware will refuse to trigger the cameras and flicker the LEDs.
Current solution: linear stage needs to be connected through serial port to a computer instead of microcontroller used for the analog projector.

## TODO
1. write more details on how to use this
2. add CAD model and picture