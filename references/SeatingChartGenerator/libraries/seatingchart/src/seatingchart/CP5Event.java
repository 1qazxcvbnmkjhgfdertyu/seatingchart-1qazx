package seatingchart;

import controlP5.*;

public class CP5Event {
  private Object control;
  private CP5EventAction action;
  
  public CP5Event(Object c, CP5EventAction a) {
    control = c;
    action = a;
  }
  
  public void run(ControlEvent e) {
    System.out.println("Running action");
    action.run(e);
  }
  
  public void runIfMatch(ControlEvent e) {
    Object o = null;
    if(e.isController())
      o = e.getController();
    if(e.isGroup())
      o = e.getGroup();
    if(e.isTab())
      o = e.getTab();
    
    if(o == control) {
      System.out.println("Matched action");
      run(e);
    }
  }
}
