javac -cp "$HOME/processing-3.1.1/core/library/core.jar:$HOME/sketchbook/libraries/controlP5/library/controlP5.jar:../poi/library/poi.jar" src/seatingchart/*.java -d bin || { exit 1; }
cd bin
jar cvf ../library/seatingchart.jar seatingchart/*.class || { exit 1; }
cd ..
cp -v -r . $HOME/sketchbook/libraries/seatingchart
