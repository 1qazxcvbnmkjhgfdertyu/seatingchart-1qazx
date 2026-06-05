package seatingchart;

import processing.core.*;

public class ImageDeskFileReader implements DeskFileReader {
  static final float brightnessThreshold = 127;
  public boolean[][] read(PApplet applet, String file) {
    PImage deskImage = applet.loadImage(file);
    
    if(deskImage == null) {
      System.out.println("Not a valid image file.");
      return null;
    }
    
    boolean[][] desks = new boolean[deskImage.width][deskImage.height];
    
    for(int y = 0; y < deskImage.height; y++) {
      for(int x = 0; x < deskImage.width; x++) {
        int c = deskImage.get(x, y);
        if(applet.brightness(c) <= brightnessThreshold) {
          desks[x][y] = true;
        } else {
          desks[x][y] = false;
        }
      }
    }
    
    return desks;
  }
}
