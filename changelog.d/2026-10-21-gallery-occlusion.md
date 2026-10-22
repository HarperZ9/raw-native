- **Gallery occlusion** (`material-gallery --occlusion`). A visibility pass gives every gallery
  sample its diffuse, specular and coat visibility past the other spheres, and both the CPU
  reference and the GPU shading pass apply it to image lighting. The material gallery's error
  against path tracer v3 falls from 0.68 to 0.57, under its 0.6 bound; without the switch every
  output is unchanged.
