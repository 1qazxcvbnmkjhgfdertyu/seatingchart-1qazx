package seatingchart;

import java.io.IOException;
import java.util.List;

public interface DataFile {
  public boolean write(ClassGroup classGroup, SeatingArrangement arrangement, boolean writeConstraintData, boolean writeSeatingData) throws IOException; //If classGroup is null, won't write student data. If arrangement is null, won't write desk data
  
  public double getFileVersion();
  
  public boolean hasStudentData();
  public boolean hasConstraintData();
  public boolean hasDeskData();
  public boolean hasSeatingData();
  
  public ClassGroup readStudents() throws IOException;
  public SeatingArrangement readDesks() throws IOException;
  public boolean addStudentsToDesks(ClassGroup classGroup, SeatingArrangement currentArrangement, List<Student> studentList) throws IOException;
  public boolean addConstraintsToStudents(ClassGroup classGroup, SeatingArrangement arrangement, List<ConstraintType> constraintTypes) throws IOException;
}
