package seatingchart;

import processing.core.*;
import controlP5.*;
import java.util.*;

public class DrawCanvas extends Canvas {
  List<CDrawable> drawnObjects;
  
  public void add(CDrawable d) {
    drawnObjects.add(d);
  }
  
  public DrawCanvas() {
    drawnObjects = new ArrayList<CDrawable>();
  }
  
  public void draw(PGraphics a) {
    for(CDrawable d : drawnObjects) {
      d.draw(a);
    }
  }
}
