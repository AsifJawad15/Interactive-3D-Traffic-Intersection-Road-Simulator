set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# The vcpkg GLAD port otherwise generates a compatibility-profile loader.
set(GLAD_PROFILE core)
