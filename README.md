# voxel-raymarching
Implementation of two voxel ray marching algorithms.

Created for my Master's thesis with the title "Tradeoffs between different voxel rendering techniques for static scenes" at Hochschule der Medien, Stuttgart

## Requirements
- VS Code extensions: C/C++, CMake Tools
- CMake version 4.0 or greater
- GCC
- OpenMP

## Branches
- *main*: Implementation of Sparse 64-trees based on "[A guide to fast voxel ray tracing using sparse 64-trees]"(https://dubiousconst282.github.io/2024/10/03/voxel-ray-tracing/) with support for loading MagicaVoxel's .vox file format
- *basic-dda*: Basic DDA approach based on Amanatides & Woo's "*A Fast Voxel Traversal Algorithm for Ray Tracing*"
