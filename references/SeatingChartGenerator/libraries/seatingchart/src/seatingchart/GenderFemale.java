package seatingchart;

public class GenderFemale implements Gender {
  public String toString() {
    return "female";
  }
  
  public char toChar() {
    return 'F';
  }
  
  public int toInt() {
    return 2;
  }
}
