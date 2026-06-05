import controlP5.*;
import seatingchart.*;

import java.util.*;
import javax.swing.JOptionPane;

color backgroundColor = color(127, 127, 127);

void setup() {
  size(768, 704); //24*32 = 768, 20*32 + 64 = 704
  setupGUI();
}

void draw() {
  background(backgroundColor);
  updateGUI();
}