# Walnut-CGB vendored core

This folder contains the `walnut_cgb.h` header from the Ink-boy integration. Its upstream MIT license and SameBoy-derived-code notice are preserved at the top of the header. Keep those notices with redistribution and modifications.

EinkBoy compiles this header once through `src/apps_local/einkboy/walnut_cgb_impl.c`; it must not be included directly by C++ files. `walnut_cgb_port.h` provides the opaque C interface and `walnut_cgb_color.h` provides the RGB565-to-luminance conversion helpers.
