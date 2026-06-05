package seatingchart;

import java.util.*;

public class ClassGroup {
  private List<Student> students;
  
  public ClassGroup() {
    students = new ArrayList<Student>();
  }
  
  public Student addStudent(Student s) {
    students.add(s);
    return s;
  }
  
  public Student removeStudent(Student s) {
    students.remove(s);
    return s;
  }
  
  public void clearStudents() {
    students.clear();
  }
  
  public void addStudents(Collection<Student> s) {
    students.addAll(s);
  }
  
  public List<Student> getStudents() {
    return new ArrayList<Student>(students);
  }
}
