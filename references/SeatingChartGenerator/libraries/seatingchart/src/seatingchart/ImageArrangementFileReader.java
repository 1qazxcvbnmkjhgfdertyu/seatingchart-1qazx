package seatingchart;

import processing.core.*;

public class ImageArrangementFileReader implements ArrangementFileReader {
  static final float brightnessThreshold = 127;
  
  public SeatingArrangement read(PApplet applet, String file) {
    System.out.println("Reading desk file " + file);
    applet.colorMode(PApplet.RGB, 255);
    
    PImage image = applet.loadImage(file);
    
    if(image == null) {
      System.out.println("Not a valid image file.");
      return null;
    }
    
    SeatingArrangement arrangement = new SeatingArrangement(image.width, image.height);
    
    System.out.println();
    for(int y = 0; y < image.height; y++) {
      for(int x = 0; x < image.width; x++) {
        int c = image.get(x, y);
        if(applet.brightness(c) <= brightnessThreshold) {
          System.out.print("*");
          arrangement.setDesk(new Desk(arrangement), x, y);
        } else {
          System.out.print(" ");
        }
      }
      System.out.println();
    }
    
    System.out.println("Done reading file.");
    return arrangement;
  }
}
