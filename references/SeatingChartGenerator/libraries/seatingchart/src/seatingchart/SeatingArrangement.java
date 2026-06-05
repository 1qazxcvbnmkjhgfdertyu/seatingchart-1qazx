package seatingchart;

import java.util.*;

public class SeatingArrangement {
  private int xLen;
  private int yLen;
  private Desk[][] desks;
  
  private List<Desk> deskList;
  
  public SeatingArrangement(int roomSizeX, int roomSizeY) {
    xLen = roomSizeX;
    yLen = roomSizeY;
    desks = new Desk[xLen][yLen];
    deskList = new ArrayList<Desk>();
  }
  
  public int getXSize() {
    return xLen;
  }
  
  public int getYSize() {
    return yLen;
  }
  
  public void setSize(int newX, int newY) {
    deskList.clear();
    
    Desk[][] newDesks = new Desk[newX][newY];
    
    for(int y = 0; y < yLen; y++) {
      for(int x = 0; x < xLen; x++) {
        if(x >= newX || y >= newY)
          continue;
        
        newDesks[x][y] = desks[x][y];
        addDesk(desks[x][y]);
      }
    }
    
    desks = newDesks;
    xLen = newX;
    yLen = newY;
  }
  
  private void addDesk(Desk d) {
    if(d != null) {
      deskList.add(d);
    }
  }
  
  private void removeDesk(Desk d) {
    if(d != null) {
      deskList.remove(d);
    }
  }
  
  public List<Desk> getDesks() {
    List<Desk> l = new ArrayList<Desk>(deskList);
    return l;
  }
  
  public Desk getDesk(int x, int y) {
    if(x >= xLen || x < 0 || y >= yLen || y < 0)
      return null;
    
    return desks[x][y];
  }
  
  public Desk setDesk(Desk d, int x, int y) {
    if(x < xLen || x >= 0 || y < yLen || y >= 0) {
      removeDesk(desks[x][y]);
      
      desks[x][y] = d;
      addDesk(d);
    }
    
    if(d != null)
      d.setPosition(x, y);
    
    return d;
  }
  
  public Desk copyDesk(int startX, int startY, int endX, int endY) {
    Desk d = getDesk(startX, startY);
    setDesk(d, endX, endY);
    
    return d;
  }
  
  public Desk moveDesk(int startX, int startY, int endX, int endY) {
    Desk d = copyDesk(startX, startY, endX, endY);
    setDesk(null, startX, startY);
    
    return d;
  }
  
  public SeatingArrangement copy(boolean copyDesks) {
    SeatingArrangement arrangement = new SeatingArrangement(getXSize(), getYSize());
    
    for(int y = 0; y < getYSize(); y++) {
      for(int x = 0; x < getXSize(); x++) {
        Desk d = getDesk(x, y);
        if(d != null) {
          if(copyDesks)
            d = d.copy();
          d.setSeatingArrangement(arrangement);
        }
        arrangement.setDesk(d, x, y);
      }
    }
    
    return arrangement;
  }
}
