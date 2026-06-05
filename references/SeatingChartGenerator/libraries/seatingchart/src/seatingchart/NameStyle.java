package seatingchart;

public enum NameStyle {
  FIRST, MIDDLE, LAST, FIRST_LAST, FULL, FULL_MIDDLE_INITIAL, INITIALS2, INITIALS3, FIRST_LAST_INITIAL;
  
  public String formatName(String n1, String n2, String n3) {
    if(n1.isEmpty())
      n1 = " ";
    if(n2.isEmpty())
      n2 = " ";
    if(n3.isEmpty())
      n3 = " ";
    
    switch(this) {
      case FIRST:
        return n1;
      case MIDDLE:
        return n2;
      case LAST:
        return n3;
      case FIRST_LAST:
        return n1 + " " + n3;
      case FULL:
        return n1 + " " + n2 + " " + n3;
      case FULL_MIDDLE_INITIAL:
        return n1 + " " + n2.charAt(0) + ". " + n3;
      case INITIALS2:
        return "" + n1.charAt(0) + n3.charAt(0);
      case INITIALS3:
        return "" + n1.charAt(0) + n2.charAt(0) + n3.charAt(0);
      case FIRST_LAST_INITIAL:
        return (n1 + " " + n3.charAt(0)).trim() + ".";
      
      default:
        return "";
    }
  }
}
