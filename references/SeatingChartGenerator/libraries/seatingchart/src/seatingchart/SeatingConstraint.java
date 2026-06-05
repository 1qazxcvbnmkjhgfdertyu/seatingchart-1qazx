package seatingchart;

import controlP5.*;
import processing.data.*;
import java.util.*;

public interface SeatingConstraint {
  public boolean satisfiesConstraint(Desk d);
  
  public String toString();
  
  public List<ControllerInterface> createGUI(ControlP5 cp5, Group g);
  
  public JSONObject createJSON();
}
