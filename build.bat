@echo off
call emsdk_env
echo Building...
em++ main.cpp -o snake.html -lidbfs.js --preload-file assets -s USE_SDL=2 -s USE_SDL_MIXER=2 -s USE_SDL_TTF=2 -s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE='$ccall' -s EXPORTED_FUNCTIONS="['_main','_filesystem_loaded']" -s EXPORTED_RUNTIME_METHODS="['ccall']"
echo Build finished.
pause