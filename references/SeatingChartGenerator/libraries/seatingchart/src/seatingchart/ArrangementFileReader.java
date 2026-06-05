package seatingchart;

import processing.core.*;

public interface ArrangementFileReader {
  public SeatingArrangement read(PApplet applet, String file);
}
