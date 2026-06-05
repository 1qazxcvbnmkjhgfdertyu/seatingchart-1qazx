package seatingchart;

public interface Named {
  public String getFirstName();
  public String getMiddleName();
  public String getLastName();
  
  public String setFirstName(String name);
  public String setMiddleName(String name);
  public String setLastName(String name);
  
  public String getName(NameStyle style);
}
