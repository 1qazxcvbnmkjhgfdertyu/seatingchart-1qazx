package seatingchart;

import java.util.*;

public class Student implements Named {
  private String firstName;
  private String middleName;
  private String lastName;
  private Gender gender;
  
  private List<SeatingConstraint> constraints;
  
  private List<Desk> bannedDesks;
  
  public Student() {
    firstName = "";
    middleName = "";
    lastName = "";
    gender = new GenderNone();
    init();
  }
  
  public Student(String n1, String n2, String n3, Gender g) {
    setFirstName(n1);
    setMiddleName(n2);
    setLastName(n3);
    gender = g;
    init();
  }
  
  private void init(){
    constraints = new ArrayList<SeatingConstraint>();
    bannedDesks = new ArrayList<Desk>();
  }
  
  public Gender getGender() {
    return gender;
  }
  
  public Gender setGender(Gender g) {
    return gender = g;
  }
  
  public String getFirstName() {
    return firstName;
  }
  
  public String setFirstName(String name) {
    return firstName = name;
  }
  
  public String getMiddleName() {
    return middleName;
  }
  
  public String setMiddleName(String name) {
    return middleName = name;
  }
  
  public String getLastName() {
    return lastName;
  }
  
  public String setLastName(String name) {
    return lastName = name;
  }
  
  public String getName(NameStyle style) {
    return style.formatName(firstName, middleName, lastName);
  }
  
  
  public SeatingConstraint addConstraint(SeatingConstraint c) {
    constraints.add(c);
    return c;
  }
  
  public SeatingConstraint removeConstraint(SeatingConstraint c) {
    constraints.remove(c);
    return c;
  }
  
  public boolean satisfiesConstraints(Desk d) {
    for(SeatingConstraint c : constraints) {
      if(!c.satisfiesConstraint(d))
        return false;
    }
    
    return true;
  }
  
  public List<SeatingConstraint> getConstraints() {
    List<SeatingConstraint> returnList = new ArrayList<SeatingConstraint>(constraints);
    return returnList;
  }
  
  
  public Desk banDesk(Desk d) {
    bannedDesks.add(d);
    return d;
  }
  
  public Desk allowDesk(Desk d) {
    bannedDesks.remove(d);
    return d;
  }
  
  public void allowAllDesks(SeatingArrangement a) {
    bannedDesks.clear();
  }
  
  public void banAllDesks(SeatingArrangement a) {
    bannedDesks.clear();
    bannedDesks.addAll(a.getDesks());
  }
  
  public List<Desk> getAllowedDesks(SeatingArrangement a) {
    List<Desk> desks = a.getDesks();
    desks.removeAll(bannedDesks);
    return desks;
  }
  
  public void clearAllConstraints() {
    bannedDesks.clear();
    constraints.clear();
  }
}
