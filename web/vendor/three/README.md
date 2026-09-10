Three.js 0.185.1, MIT license (see LICENSE).

The two distribution modules are copied unmodified from the `three@0.185.1`
npm package. OrbitControls comes from the same package; Rolldown minifies it
and changes its `three` import to the local module path.

These modules load from this site only when challenge two opens. There are
no runtime CDN requests. The static deployment includes this entire directory.

To reproduce after installing the existing IDE build dependencies, run:

    node scripts/vendor_farm_three.mjs

The script downloads the pinned npm package, copies its distributions and
license, and builds OrbitControls with the repository's installed Rolldown.
