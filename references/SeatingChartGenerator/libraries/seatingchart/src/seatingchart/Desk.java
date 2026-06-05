package seatingchart;

public class Desk {
  private SeatingArrangement arrangement;
  private int x;
  private int y;
  private Student student;
  
  
  public Desk(SeatingArrangement a) {
    arrangement = a;
    student = null;
    x = 0;
    y = 0;
  }
  
  public Desk(SeatingArrangement a, Student s) {
    arrangement = a;
    student = s;
    x = 0;
    y = 0;
  }
  
  
  public Student getStudent() {
    return student;
  }
  
  public Student setStudent(Student s) {
    return student = s;
  }
  
  public int getX() {
    return x;
  }
  
  public int getY() {
    return y;
  }
  
  public void setPosition(int x, int y) {
    this.x = x;
    this.y = y;
  }
  
  public SeatingArrangement getSeatingArrangement() {
    return arrangement;
  }
  
  public SeatingArrangement setSeatingArrangement(SeatingArrangement a) {
    arrangement = a;
    return a;
  }
  
  public Desk copy() {
    Desk newDesk = new Desk(getSeatingArrangement(), getStudent());
    newDesk.setPosition(getX(), getY());
    return newDesk;
  }
}
