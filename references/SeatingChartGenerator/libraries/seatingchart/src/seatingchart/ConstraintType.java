package seatingchart;

import processing.data.*;

public interface ConstraintType {
  public String toString();
  public boolean matchesType(SeatingConstraint c);
  public SeatingConstraint create();
  public SeatingConstraint createFromJSON(JSONObject json);
}
