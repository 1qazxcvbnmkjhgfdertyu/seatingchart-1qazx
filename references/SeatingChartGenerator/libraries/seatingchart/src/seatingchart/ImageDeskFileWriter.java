package seatingchart;

import processing.core.*;

public class ImageDeskFileWriter implements DeskFileWriter {
  public boolean write(PApplet applet, String file, boolean[][] desks) {
    System.out.println("Writing desk file " + file);
    
    int xStart = getXStart(desks);
    int xEnd = getXEnd(desks);
    int yStart = getYStart(desks);
    int yEnd = getYEnd(desks);
    
    System.out.println("Start at: (" + xStart + ", " + yStart + ")");
    System.out.println("End at: (" + xEnd + ", " + yEnd + ")");
    
    PImage image = applet.createImage(xEnd - xStart, yEnd - yStart, PApplet.RGB);
    
    for(int y = 0; y < image.height; y++) {
      for(int x = 0; x < image.width; x++) {
        int c;
        boolean value = desks[xStart + x][yStart + y];
        if(value) {
          c = applet.color(0);
        } else {
          c = applet.color(255);
        }
        
        image.set(x, y, c);
      }
    }
    
    image.save(file);
    
    System.out.println("Done writing file");
    return true;
  }
  
  private int getXStart(boolean[][] array) {
    for(int x = 0; x < array.length; x++) {
      for(int y = 0; y < array[0].length; y++) {
        if(array[x][y])
          return x;
      }
    }
    
    return 0;
  }
  
  private int getXEnd(boolean[][] array) {
    for(int x = array.length - 1; x >= 0; x--) {
      for(int y = 0; y < array[0].length; y++) {
        if(array[x][y])
          return x + 1;
      }
    }
    
    return 1;
  }
  
  private int getYStart(boolean[][] array) {
    for(int y = 0; y < array[0].length; y++) {
      for(int x = 0; x < array.length; x++) {
        if(array[x][y])
          return y;
      }
    }
    
    return 0;
  }
  
  private int getYEnd(boolean[][] array) {
    for(int y = array[0].length - 1; y >= 0; y--) {
      for(int x = 0; x < array.length; x++) {
        if(array[x][y])
          return y + 1;
      }
    }
    
    return 1;
  }
  
  
}
