package seatingchart;

import processing.core.*;

public interface DeskFileWriter {
  public boolean write(PApplet applet, String file, boolean[][] desks);
}
