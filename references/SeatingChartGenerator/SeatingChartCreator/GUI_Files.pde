void genericFileOpen(File f) {
  if(f == null)
    return;
  
  String extension = getFileExtension(f.toString());
  
  if(extension.equals("xls")) {
    classFileLoaded(f);
    return;
  }
  
  if(extension.equals("png") || extension.equals("gif") || extension.equals("bmp")) {
    deskFileLoaded(f);
    return;
  }
  
  if(extension.equals("scdata")) {
    dataFileLoaded(f);
    return;
  }
  
  errorMessage("Error reading file.\nMake sure file is in the correct format.");
}



void classFileLoaded(File f) {
  if(f == null)
    return;
  
  if(!getFileExtension(f.toString()).equals("xls")) {
    errorMessage("Please choose a file of type '.xls'.");
    return;
  }
  
  clearAllDesks();
  String fileName = f.toString();
  readClass(fileName);
}

void readClass(String s) {
  ClassGroup result = new ExcelClassFileReader().read(this, s);
  
  if(result == null) {
    errorMessage("Error reading class file.\nMake sure file is in the correct format.");
    return;
  }
  
  currentClass = result;
  currentClassFile = s;
  updateCurrentClass();
}


void deskFileLoaded(File f) {
  if(f == null)
    return;
  
  SeatingArrangement result = new ImageArrangementFileReader().read(this, f.toString());
  if(result == null) {
    errorMessage("Error reading desk file.\nMake sure file is in the correct format\n(should be an image file, such as '.png').");
    return;
  }
  
  clearAllDesks();
  currentSeatingChart = result;
  updateCurrentSeatingChart();
}

boolean writeDataFile(Path p, boolean writeStudentData, boolean writeConstraintData, boolean writeDeskData, boolean writeSeatingData) {
  if(p == null)
    return false;
  
  String fileName = p.toString();
  
  println("Writing data file " + fileName);
  
  if(!fileName.endsWith(".scdata")) {
    fileName = fileName + ".scdata";
    p = Paths.get(fileName);
  }
  
  DataFile file;
  
  try {
    file = new DataFileVersion2(this, p);
    return file.write(writeStudentData ? currentClass : null, writeDeskData ? currentSeatingChart : null, writeConstraintData, writeSeatingData);
  } catch (IOException e) {
    errorMessage(e.getMessage());
    return false;
  } catch (NullPointerException e) {
    errorMessage("Error saving file: " + e.toString());
    return false;
  }
  
}


void seatingFileSaved(File f) {
  if(f == null)
    return;
  
  boolean result = writeDataFile(f.toPath(), true, true, true, true); //Write all data
  
  if(!result) {
    errorMessage("Error saving seating chart file.");
  }
}


void constraintFileSaved(File f) {
  if(f == null)
    return;
  
  boolean result = writeDataFile(f.toPath(), true, true, true, false); //Write all data but seating
  
  if(!result) {
    errorMessage("Error saving constraint file.");
  }
}


void dataFileLoaded(File f) {
  if(f == null)
    return;
  
  try {
    DataFile file = new DataFileVersion2(this, f.toPath());
    
    clearAllDesks();
    
    if(file.hasStudentData()) {
      println("Reading student data...");
      ClassGroup result = file.readStudents();
  
      if(result == null) {
        errorMessage("Error reading data file.\nMake sure file is in the correct format.");
        return;
      }
      
      currentClass = result;
      currentClassFile = f.toString();
      updateCurrentClass();
    }
    if(file.hasDeskData()) {
      println("Reading desk data...");
      SeatingArrangement result = file.readDesks();
      if(result == null) {
        errorMessage("Error reading data file.\nMake sure file is in the correct format.");
        return;
      }
      currentSeatingChart = result;
      updateCurrentSeatingChart();
    }
    if(file.hasSeatingData()) {
      println("Reading seating data...");
      boolean result = file.addStudentsToDesks(currentClass, currentSeatingChart, currentStudentList);
      updateCurrentSeatingChart();
      updateStudentList();
      if(result == false) {
        errorMessage("Error reading data file.\nMake sure file is in the correct format.");
        return;
      }
    }
    if(file.hasConstraintData()) {
      println("Reading constraint data...");
      boolean result = file.addConstraintsToStudents(currentClass, currentSeatingChart, constraints);
      updateCurrentSeatingChart();
      updateStudentList();
      if(result == false) {
        errorMessage("Error reading data file.\nMake sure file is in the correct format.");
        return;
      }
    }
  } catch (Exception e) {
    e.printStackTrace();
    errorMessage("Error: " + e.toString());
    
    return;
  }
}