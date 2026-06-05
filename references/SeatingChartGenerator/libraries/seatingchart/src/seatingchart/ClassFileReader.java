package seatingchart;

import processing.core.*;

public interface ClassFileReader {
  public ClassGroup read(PApplet applet, String file);
}
