package seatingchart;

import processing.core.*;
import controlP5.*;

public class Rectangle implements CDrawable {
  int xPos;
  int yPos;
  int xLen;
  int yLen;
  
  int fill;
  int stroke;
  
  public Rectangle(int _xPos, int _yPos, int _xLen, int _yLen, int _fill, int _stroke) {
    xPos = _xPos;
    yPos = _yPos;
    xLen = _xLen;
    yLen = _yLen;
    fill = _fill;
    stroke = _stroke;
  }
  
  public void draw(PGraphics g) {
    g.fill(fill);
    g.stroke(stroke);
    g.rectMode(PGraphics.CORNER);
    g.rect(xPos, yPos, xLen, yLen);
  }
}
