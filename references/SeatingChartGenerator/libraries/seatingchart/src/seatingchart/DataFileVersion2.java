package seatingchart;

import java.util.*;
import java.io.IOException;
import java.nio.file.*;
import processing.core.*;
import processing.data.*;

public class DataFileVersion2 implements DataFile {
  private static final float VERSION = 2.0f;
  private static final float VERSION_LOWER = 2.0f; //Lowest version number this can read
  private static final float VERSION_UPPER = 3.0f; //Highest version number
  
  private PApplet applet;
  
  private JSONObject json;
  private float thisFileVersion;
  private boolean hasStudentData;
  private boolean hasConstraintData;
  private boolean hasDeskData;
  private boolean hasSeatingData;
  
  private Path file;
  
  public DataFileVersion2(PApplet applet, Path f) throws IOException {
    this.applet = applet;
    file = f;
    
    if(file.toFile().exists()) {
      loadFile();
    } else {
      thisFileVersion = 0;
      hasStudentData = false;
      hasConstraintData = false;
      hasDeskData = false;
      hasSeatingData = false;
    }
  }
  
  private Student findStudent(String fullName, List<Student> studentList) {
    fullName = fullName.toLowerCase().trim();
    for(Student s : studentList) {
      String sName = s.getName(NameStyle.FULL);
      sName = sName.toLowerCase().trim();
      if(sName.equals(fullName))
        return s;
    }
    
    return null;
  }
  
  
  private void loadFile() throws IOException {
    System.out.println("Loading data file...");
    try {
      //Load the file
      json = applet.loadJSONObject(file.toString());
      if(json == null) {
        throw new IOException("Could not load " + file.toString() + ".\nMake sure file is in the correct format.");
      }
      
      //Get some data from the file so simple operations don't require rereading
      //the file.
      
      //The file version
      thisFileVersion = json.getFloat("version");
      //Shouldn't be too low or too high
      if(thisFileVersion < VERSION_LOWER) {
        throw new IOException("This file was created with an older version of Seating Chart Creator and cannot be read.");
      }
      if(thisFileVersion > VERSION_LOWER) {
        throw new IOException("This file was created with a newer version of Seating Chart Creator and cannot be read.");
      }
      
      //The data in the file
      hasStudentData = json.getBoolean("studentData");
      hasConstraintData = json.getBoolean("constraintData");
      hasDeskData = json.getBoolean("deskData");
      hasSeatingData = json.getBoolean("seatingData");
    } catch (RuntimeException e) {
      throw new IOException("Could not load " + file.toString() + ".\nMake sure file is in the correct format.");
    }
  }
  
  public double getFileVersion() {
    return thisFileVersion;
  }
  
  public boolean hasStudentData() {
    return hasStudentData;
  }
  public boolean hasConstraintData() {
    return hasConstraintData;
  }
  public boolean hasDeskData() {
    return hasDeskData;
  }
  public boolean hasSeatingData() {
    return hasSeatingData;
  }
  
  public ClassGroup readStudents() throws IOException {
    if(!hasStudentData) {
      return null;
    }
    
    JSONArray array = json.getJSONArray("Student");
    if(array == null)
      return null;
    
    ClassGroup c = new ClassGroup();
    for(int i = 0; i < array.size(); i++) {
      JSONObject studentObject = array.getJSONObject(i);
      if(studentObject == null)
        return null;
      Student s = new Student();
      
      s.setFirstName(studentObject.getString("firstName"));
      s.setMiddleName(studentObject.getString("middleName"));
      s.setLastName(studentObject.getString("lastName"));
      switch(studentObject.getInt("gender")) {
        case 0:
          s.setGender(new GenderNone());
          break;
        case 1:
          s.setGender(new GenderMale());
          break;
        case 2:
          s.setGender(new GenderFemale());
          break;
      }
      
      c.addStudent(s);
    }
    
    return c;
  }
  
  
  public SeatingArrangement readDesks() throws IOException {
    if(!hasStudentData) {
      return null;
    }
    
    JSONArray deskArray = json.getJSONArray("Desk");
    if(deskArray == null)
      return null;
    
    SeatingArrangement arrangement = new SeatingArrangement(json.getInt("deskXLen"), json.getInt("deskYLen"));
    
    for(int i = 0; i < deskArray.size(); i++) {
      JSONObject deskObject = deskArray.getJSONObject(i);
      if(deskObject == null)
        return null;
      
      arrangement.setDesk(new Desk(arrangement), deskObject.getInt("x"), deskObject.getInt("y"));
    }
    
    return arrangement;
  }
  
  
  public boolean addStudentsToDesks(ClassGroup classGroup, SeatingArrangement currentArrangement, List<Student> studentList) throws IOException {
    if(!hasSeatingData) {
      return false;
    }
    
    JSONArray studentArray = json.getJSONArray("Student");
    if(studentArray == null)
      return false;
    
    for(int i = 0; i < studentArray.size(); i++) {
      JSONObject studentObject = studentArray.getJSONObject(i);
      if(studentObject == null)
        return false;
      
      String firstName = studentObject.getString("firstName");
      String middleName = studentObject.getString("middleName");
      String lastName = studentObject.getString("lastName");
      String fullName = NameStyle.FULL.formatName(firstName, middleName, lastName);
      
      Student s = findStudent(fullName, studentList);
      if(s == null)
        return false;
      
      //Default values are -1 because sometimes if the student isn't in a desk, deskX or deskY won't be set
      int deskX = studentObject.getInt("deskX", -1);
      int deskY = studentObject.getInt("deskY", -1);
      
      if(deskX != -1 && deskY != -1) {
        Desk d = currentArrangement.getDesk(deskX, deskY);
        if(d == null) {
          System.out.println("Desk not found!");
          return false;
        }
        
        d.setStudent(s);
        studentList.remove(s);
      }
      
    }
    
    return true;
  }
  
  
  public boolean addConstraintsToStudents(ClassGroup classGroup, SeatingArrangement arrangement, List<ConstraintType> constraintTypes) throws IOException {
    JSONArray studentArray = json.getJSONArray("Student");
    if(studentArray == null) {
      System.out.println("No student array!");
      return false;
    }
    
    List<Student> studentList = classGroup.getStudents();
    
    for(int i = 0; i < studentArray.size(); i++) {
      JSONObject studentObject = studentArray.getJSONObject(i);
      if(!readStudentObject(studentObject, studentList, arrangement, constraintTypes)) {
        System.out.println("Error reading student object!");
        return false;
      }
    }
    
    
    return true;
  }
  
  private boolean readStudentObject(JSONObject studentObject, List<Student> studentList, SeatingArrangement arrangement, List<ConstraintType> constraints) {
    String firstName = studentObject.getString("firstName");
    String middleName = studentObject.getString("middleName");
    String lastName = studentObject.getString("lastName");
    String fullName = NameStyle.FULL.formatName(firstName, middleName, lastName);
    Student s = findStudent(fullName, studentList);
    if(s == null) {
      System.out.println("Couldn't find student " + fullName);
      return false;
    }
    
    JSONArray deskArray = studentObject.getJSONArray("DeskRule");
    if(deskArray != null) {
      for(int i = 0; i < deskArray.size(); i++) {
        JSONObject deskObject = deskArray.getJSONObject(i);
        if(!readDeskObject(deskObject, s, arrangement)) {
          System.out.println("Error reading desk object!");
          return false;
        }
      }
    }
    
    JSONArray constraintArray = studentObject.getJSONArray("Constraint");
    if(constraintArray != null) {
      for(int i = 0; i < constraintArray.size(); i++) {
        JSONObject constraintObject= constraintArray.getJSONObject(i);
        if(!readConstraintObject(constraintObject, s, constraints)) {
          System.out.println("Error reading constraint object!");
          return false;
        }
      }
    }
    
    return true;
  }
  
  private boolean readDeskObject(JSONObject deskObject, Student student, SeatingArrangement arrangement) {
    int x = deskObject.getInt("x");
    int y = deskObject.getInt("y");
    
    Desk d = arrangement.getDesk(x, y);
    if(d == null) {
      System.out.println("No desk at " + x + ", " + y);
      return false;
    }
    
    boolean allowed = deskObject.getBoolean("allowed");
    
    if(allowed) {
      student.allowDesk(d);
    } else {
      student.banDesk(d);
    }
    
    return true;
  }
  
  private boolean readConstraintObject(JSONObject constraintObject, Student student, List<ConstraintType> constraints) {
    String type = constraintObject.getString("type");
    
    ConstraintType constraintType = findConstraint(type, constraints);
    if(constraintType == null) {
      System.out.println("Couldn't find constraint type " + type);
      return false;
    }
    
    SeatingConstraint constraint = constraintType.createFromJSON(constraintObject);
    if(constraint == null) {
      System.out.println("Error creating constraint " + type);
      return false;
    }
    student.addConstraint(constraint);
    
    return true;
  }
  
  private ConstraintType findConstraint(String name, List<ConstraintType> constraints) {
    for(ConstraintType c : constraints) {
      String cName = c.toString();
      if(cName.equals(name))
        return c;
    }
    
    System.out.println("Couldn't find constraint type " + name);
    return null;
  }
  
  
  
  
  
  public boolean write(ClassGroup classGroup, SeatingArrangement arrangement, boolean writeConstraintData, boolean writeSeatingData) throws IOException {
    System.out.println("Writing data file...");
    
    JSONObject json = new JSONObject();
    
    //Set the file version
    json.setFloat("version", VERSION);
    
    //If a class was given, there must be student data and also potentially
    //constraint data and seating data
    if(classGroup != null) {
      json.setBoolean("studentData", true);
      
      if(writeConstraintData) {
        json.setBoolean("constraintData", true);
      } else {
        json.setBoolean("constraintData", false);
      }
      
      //There can't be seating data if there isn't a seating arrangement given
      if(writeSeatingData && arrangement != null) {
        json.setBoolean("seatingData", true);
      } else {
        json.setBoolean("seatingData", false);
      }
      
      json.setJSONArray("Student", createStudentArray(classGroup, arrangement, writeConstraintData, writeSeatingData));
    } else {
      json.setBoolean("studentData", false);
      json.setBoolean("constraintData", false);
      json.setBoolean("seatingData", false);
    }
    
    //If a seating arrangement was given, there must be seating data
    if(arrangement != null) {
      json.setBoolean("deskData", true);
      
      json.setJSONArray("Desk", createDeskArray(arrangement));
      json.setInt("deskXLen", arrangement.getXSize());
      json.setInt("deskYLen", arrangement.getYSize());
    } else {
      json.setBoolean("deskData", false);
    }
    
    //Save the file
    applet.saveJSONObject(json, file.toString());
    
    //Update file information by loading it again
    loadFile();
    
    //Everything went well
    return true;
  }
  
  private JSONArray createStudentArray(ClassGroup classGroup, SeatingArrangement arrangement, boolean constraints, boolean seatingData) {
    JSONArray studentArray = new JSONArray();
    
    //Create a map of students and the desks they sit at (Student objects don't
    //store this information)
    Map<Student, Desk> seating = new HashMap<Student, Desk>();
    if(seatingData) {
      for(Desk d : arrangement.getDesks()) {
        seating.put(d.getStudent(), d);
      }
    }
    
    //For each student, make a JSON object and add it to the array
    int i = 0;
    for(Student s : classGroup.getStudents()) {
      studentArray.setJSONObject(i, createStudentObject(s, arrangement, constraints, seating));
      
      i++;
    }
    
    return studentArray;
  }
  
  private JSONObject createStudentObject(Student student, SeatingArrangement arrangement, boolean constraints, Map<Student, Desk> seating) {
    JSONObject studentObject = new JSONObject();
    
    //Student name / gender info
    studentObject.setString("firstName", student.getFirstName());
    studentObject.setString("middleName", student.getMiddleName());
    studentObject.setString("lastName", student.getLastName());
    studentObject.setInt("gender", student.getGender().toInt());
    
    //Student desk position
    if(seating != null) {
      if(seating.containsKey(student)) {
        studentObject.setInt("deskX", seating.get(student).getX());
        studentObject.setInt("deskY", seating.get(student).getY());
      }
    }
    
    //Make constraints and allowed desk rules
    if(constraints) {
      studentObject.setJSONArray("DeskRule", createDeskRuleArray(arrangement, student.getAllowedDesks(arrangement)));
      studentObject.setJSONArray("Constraint", createConstraintsArray(student.getConstraints()));
    }
    
    return studentObject;
  }
  
  private JSONArray createDeskRuleArray(SeatingArrangement arrangement, List<Desk> allowedDesks) {
    JSONArray deskArray = new JSONArray();
    
    int i = 0;
    for(Desk d : arrangement.getDesks()) {
      JSONObject deskObject = new JSONObject();
      deskObject.setInt("x", d.getX());
      deskObject.setInt("y", d.getY());
      deskObject.setBoolean("allowed", allowedDesks.contains(d));
      
      deskArray.setJSONObject(i, deskObject);
      i++;
    }
    
    return deskArray;
  }
  
  private JSONArray createConstraintsArray(List<SeatingConstraint> constraints) {
    JSONArray constraintArray = new JSONArray();
    
    int i = 0;
    for(SeatingConstraint c : constraints) {
      JSONObject constraintObject = c.createJSON();
      
      constraintArray.setJSONObject(i, constraintObject);
      i++;
    }
    
    return constraintArray;
  }
  
  private JSONArray createDeskArray(SeatingArrangement arrangement) {
    JSONArray deskArray = new JSONArray();
    
    int i = 0;
    for(Desk d : arrangement.getDesks()) {
      JSONObject deskObject = new JSONObject();
      deskObject.setInt("x", d.getX());
      deskObject.setInt("y", d.getY());
      
      deskArray.setJSONObject(i, deskObject);
      i++;
    }
    
    return deskArray;
  }
}
