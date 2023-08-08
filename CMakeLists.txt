
cmake_minimum_required(VERSION 3.5)
project(libdnf5-plugin-swidtags)
set(CMAKE_CXX_STANDARD 20)

set(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})

# set gettext domain for translations
add_definitions(-DGETTEXT_DOMAIN=\"libdnf5\")

add_library(swidtags MODULE swidtags_plugin.cpp)
# disable the 'lib' prefix in order to create st.so
set_target_properties(swidtags PROPERTIES PREFIX "")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDNF5 REQUIRED libdnf5)
pkg_check_modules(LIBXMLXX REQUIRED libxml++-2.6)

include_directories(${LIBDNF5_INCLUDE_DIRS})
include_directories(${LIBXMLXX_INCLUDE_DIRS})

target_link_libraries(swidtags ${LIBDNF5_LIBRARIES} ${LIBXMLXX_LIBRARIES})

install(TARGETS swidtags LIBRARY DESTINATION "${LIB_INSTALL_DIR}/libdnf5/plugins/")
install(FILES swidtags.conf DESTINATION "${SYSCONF_INSTALL_DIR}/dnf/libdnf5-plugins/" COMPONENT config)
