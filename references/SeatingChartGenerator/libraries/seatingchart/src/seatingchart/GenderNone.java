package seatingchart;

public class GenderNone implements Gender {
  public String toString() {
    return "none";
  }
  
  public char toChar() {
    return 'N';
  }
  
  public int toInt() {
    return 0;
  }
}
