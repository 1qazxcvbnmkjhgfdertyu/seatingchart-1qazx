package seatingchart;

import java.util.*;

public class RecursiveRandomizer implements Randomizer {
  private int counter;
  private final int counterLimit = 4096;
  
  public SeatingArrangement randomize(SeatingArrangement arrangement, List<Student> students, boolean randomizeDesks, boolean reverseDesks) {
    System.out.println("Randomizing...");
    
    counter = 0;
    
    students = new ArrayList<Student>(students);
    
    arrangement = arrangement.copy(false); //Needs to be false, otherwise desks can't be compared to each other
    List<Desk> desks = getEmptyDesks(arrangement);
    
    students = new ListRandomizer<Student>().randomize(students);
    
    if(randomizeDesks) {
      System.out.println("Randomize desks");
      desks = new ListRandomizer<Desk>().randomize(desks);
    }
    if(reverseDesks) {
      System.out.println("Reverse desks");
      Collections.reverse(desks);
    }
    
    
    System.out.println();
    return randomizeRecursive(arrangement, students, desks);
  }
  
  
  private List<Desk> getEmptyDesks(SeatingArrangement a) {
    System.out.println("Get empty desks...");
    
    List<Desk> desks = new ArrayList<Desk>();
    for(Desk d : a.getDesks()) {
      if(d.getStudent() == null) {
        desks.add(d);
      }
    }
    
    System.out.println();
    return desks;
  }
  
  
  private SeatingArrangement randomizeRecursive(SeatingArrangement a, List<Student> remainingStudents, List<Desk> remainingDesks) {
    if(counter++ >= counterLimit) {
      return null;
    }
    
    if(remainingStudents.isEmpty()) {
      return a;
    }
    
    if(remainingDesks.isEmpty()) {
      return a;
    }
    
    
    a = a.copy(false); //Needs to be false, otherwise desks can't be compared to each other
    
    for(Student s : remainingStudents) {
      
      List<Desk> allowedDesks = s.getAllowedDesks(a);
      allowedDesks = new ArrayList<Desk>(allowedDesks);
      
      List<Desk> remainingDesksTemp = new ArrayList<Desk>(remainingDesks);
      remainingDesksTemp.retainAll(allowedDesks);
      allowedDesks = remainingDesksTemp;
      remainingDesksTemp = null;
      
      if(allowedDesks.isEmpty()) {
        continue;
      }
      
      
      for(Desk d : allowedDesks) {
        d.setStudent(s);
        if(satisfiesConstraints(a)) {
          List<Student> sl = new ArrayList<Student>(remainingStudents);
          sl.remove(s);
          List<Desk> dl = new ArrayList<Desk>(remainingDesks);
          dl.remove(d);
          SeatingArrangement result = randomizeRecursive(a, sl, dl);
          if(result != null)
            return result;
        } else {
        }
        d.setStudent(null);
      }
      
    }
    
    return null;
  }
  
  
  private boolean satisfiesConstraints(SeatingArrangement a) {
    for(Desk d : a.getDesks()) {
      if(d.getStudent() == null)
        continue;
      if(!d.getStudent().satisfiesConstraints(d))
        return false;
    }
    
    return true;
  }
}
