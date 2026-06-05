package seatingchart;

import processing.core.*;

public interface DeskFileReader {
  boolean[][] read(PApplet applet, String file);
}
