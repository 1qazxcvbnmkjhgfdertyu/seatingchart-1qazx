package seatingchart;

public class GenderMale implements Gender {
  public String toString() {
    return "male";
  }
  
  public char toChar() {
    return 'M';
  }
  
  public int toInt() {
    return 1;
  }
}
