List<ConstraintType> constraints;


void setupConstraints() {
  constraints = new ArrayList<ConstraintType>();
  
  constraints.add(new ConstraintType() {
    public String toString() {
      return "Proximity";
    }
    
    public boolean matchesType(SeatingConstraint c) {
      return new ProximityConstraint(0, false, null).getClass().isInstance(c);
    }
    
    public ProximityConstraint create() {
     return new ProximityConstraint(1, false, null);
    }
    
    public ProximityConstraint createFromJSON(JSONObject json) {
      int distance = json.getInt("distance");
      boolean mode = json.getBoolean("mode");
      String studentName = json.getString("student");
      Student student = findStudent(studentName, currentClass.getStudents());
      if(student == null) {
        println("Couldn't find student " + studentName);
        return null;
      }
      
      return new ProximityConstraint(distance, mode, student);
    }
  });
}

public class ProximityConstraint implements SeatingConstraint {
  int distance;
  boolean mode;
  Student student;
  
  public ProximityConstraint(int _distance, boolean _mode, Student _student) {
    distance = _distance;
    mode = _mode;
    student = _student;
  }
  
  public String toString() {
    String s = "Proximity ";
    
    if(student != null) {
      s = s + "to " + student.getName(NameStyle.FIRST_LAST) + " ";
    } else {
      
    }
    
    if(mode) {
      s = s + "<= ";
    } else {
      s = s + "> ";
    }
    
    s = s + distance;
    
    return s;
  }
  
  
  public boolean satisfiesConstraint(Desk d) {
    if(student == null)
      return true;
    if(distance < 1)
      return !mode;
    
    SeatingArrangement a = d.getSeatingArrangement();
    int xPos = d.getX();
    int yPos = d.getY();
    
    for(int y = yPos - distance; y < yPos + distance + 1; y++) {
      for(int x = xPos - distance; x < xPos + distance + 1; x++) {
        Desk currentDesk = a.getDesk(x, y);
        if(currentDesk == null)
          continue;
        if(currentDesk.getStudent() == student)
          return mode;
      }
    }
    
    return !mode;
  }
  
  public int getDistance() {
    return distance;
  }
  
  public int setDistance(int d) {
    distance = d;
    return d;
  }
  
  public boolean getMode() {
    return mode;
  }
  
  public boolean setMode(boolean m) {
    mode = m;
    return m;
  }
  
  public Student getStudent() {
    return student;
  }
  
  public Student setStudent(Student s) {
    student = s;
    return s;
  }
  
  
  public JSONObject createJSON() {
    JSONObject json = new JSONObject();
    json.setString("type", "Proximity");
    
    json.setInt("distance", distance);
    json.setBoolean("mode", mode);
    json.setString("student", student.getName(NameStyle.FULL));
    
    return json;
  }
  
  
  public List<ControllerInterface> createGUI(ControlP5 cp5, Group g) {
    final DropdownList ddlStudent;
    final Textfield txfDistance;
    final RadioButton rdoMode;
    
    
    final ProximityConstraint thisSeatingConstraint = this;
      
    
    txfDistance = cp5.addTextfield("Distance")
      .setPosition(4, 28)
      .setWidth(64)
      .setAutoClear(false)
      .setText(Integer.valueOf(getDistance()).toString())
      .moveTo(g)
      ;
    addEvent(txfDistance, new CP5EventAction() {
      public void run(ControlEvent e) {
        try {
          thisSeatingConstraint.setDistance(Integer.parseInt(txfDistance.getText()));
        } catch(NumberFormatException ex) {
          txfDistance.setText(Integer.valueOf(getDistance()).toString());
        }
        updateConstraintList();
      }
    });
    
    rdoMode = cp5.addRadioButton("Mode")
      .setPosition(4, 72)
      .addItem("Must be > distance", 0)
      .addItem("Must be <= distance", 1)
      .activate(getMode() ? 1 : 0)
      .moveTo(g)
      ;
    addEvent(rdoMode, new CP5EventAction() {
      public void run(ControlEvent e) {
        thisSeatingConstraint.setMode(e.getValue() != 0);
        updateConstraintList();
      }
    });
    
    
    ddlStudent = cp5.addDropdownList("StudentSelect")
      .setPosition(4, 7)
      .setSize(96, 96)
      .setBarHeight(16)
      .setOpen(false)
      //TODO
      //.actAsPulldownMenu(true)
      .moveTo(g)
      ;
    ddlStudent.getCaptionLabel().set("Student...");
    if(getStudent() != null) {
      ddlStudent.getCaptionLabel().set(getStudent().getName(NameStyle.FIRST_LAST_INITIAL));
    }
    ddlStudent.getCaptionLabel().getStyle().marginTop = 4;
    int i = 0;
    if(currentClass != null) {
      for(Student s : currentClass.getStudents()) {
        ddlStudent.addItem(s.getName(NameStyle.FIRST_LAST_INITIAL), i++);
      }
    }
    addEvent(ddlStudent, new CP5EventAction() {
      public void run(ControlEvent e) {
        int i = (int)e.getValue();
        thisSeatingConstraint.setStudent(currentClass.getStudents().get(i));
        updateConstraintList();
      }
    });
    
    
    
    List<ControllerInterface> controls = new ArrayList<ControllerInterface>(3);
    controls.add(ddlStudent);
    controls.add(txfDistance);
    controls.add(rdoMode);
    return controls;
  }
}
