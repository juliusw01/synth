#!/bin/bash

if [ ! -d "./bin" ]
then
    mkdir bin
fi

if [ ! -d "./bin-int" ]
then
    mkdir bin-int
fi

pushd ./bin-int

g++ ../main.cpp -o synth -I/opt/homebrew/include -L/opt/homebrew/lib -lportaudio -std=c++17 -framework CoreAudio -framework AudioToolbox -framework CoreServices -lportaudio

popd

mv bin-int/synth bin/synth

echo "Running code... "
echo "============================="
echo " "

cd bin

./synth

echo " "
echo "============================="
