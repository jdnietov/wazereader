#include <iostream>
#include <ctime>
#include <csignal>
#include <fstream>
#include <sstream>
#include <string>
#include <stdlib.h>
#include <errno.h>
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

#define Q 143
#define BARH 62
#define WINW 1920
#define WINH 1080
#define NMODS 6

using namespace std;
using namespace cv;

const int ZOOM      = 17;
const int PRECISION = 7;
const int ICON      = 54;
const double DLAT   = 0.0108;  // y-axis, increase upwards
const double DLNG   = 0.0206;  // x-axis, decrease left
const double DLOAD  = 5;      // console progress bar
const char* IMGNAME = "screenshot.png";
char *DIR = "icons/";
char *image_window = "Source Image";

struct Location {
  double lat;
  double lng;
};

Mat img; Mat templ; Mat result;
int match_method = CV_TM_CCOEFF_NORMED;
float threshold_min = 0.007; float threshold_max = 0.88;
bool graphicMode = false, debugMode = false, silentMode = false;
ofstream bash, data;

bool READFILES[NMODS];
char *FILENAMES[NMODS] = {
  "accidente.png", "detenido.png", "embotellamiento.png",
  "obra.png", "via_cerrada.png", "policia.png"
};
char *FILELABELS[NMODS] = {
  "accidente", "detenido", "embotellamiento",
  "obra", "via-cerrada", "policia"
};
char *FLAGNAMES[NMODS] = {
  "-a", "-d", "-e", "-o", "-v", "-p"
};
double THRESHOLDS[NMODS][2] = {
  {0.88, 0.007}, // acc
  {0.91, 0.007}, // det
  {0.88, 0.007}, // emb
  {0.92, 0.007}, // obr
  {0.88, 0.007}, // via
  {0.88, 0.007}, // pol
};

int gidx = 0;
struct Location map[Q];  // FIXME: bug - can't declare map[] before READFILES[]! 

void setup(int argc, char *argv[]);
void setupFlags(int argc, char *argv[]);

void clean();
void chksyscall(string line);
void signalHandler( int signum );

string currentDateTime();
int printProgressBar(int i, int load);
void getMapScreenshot(int i, double lat, double lng);
void generateMap();
void fillMapCol(int init, double iniLat, double iniLng, int n);

bool matches(float f);
void fetchPositions(char* imgname, char* templname, void *out, double threshold);
void getCoordinates(int pixelLng, int pixelLat, double lat, double lng, void *res);
void writeMatch(char* templname, char* label, double lat, double lng, double threshold);
void writeMatches(double lat, double lng);

// ────────────────────────────────────────────────────────────────────────────────


int main (int argc, char *argv[]) {
  cout << "WazeReader\tScan traffic events through Bogota, Colombia\n";
  cout << "\t\tBeginning execution at " << currentDateTime() << "\n\n";

  setup(argc, argv);
  generateMap(); // FIXME: some coordinates are off place at first iteration

  int load = 0;   // used in console progress bar
  for(int i = 0; i < Q; i++) {
    load = printProgressBar(i, load);

    double lat = map[i].lat;
    double lng = map[i].lng;

    getMapScreenshot(i, lat, lng);
    writeMatches(lat, lng);

    // close Chrome tab
    chksyscall("xdotool key Ctrl+w");
  }

  cout << "\Map scanning concluded at " << currentDateTime() << "\n";
  clean();
  return 0;
}

void setup(int argc, char *argv[]) {
  setupFlags(argc, argv);
  // chksyscall("./setup.sh");
  signal(SIGINT, signalHandler);

  bash.precision(PRECISION);
  data.precision(PRECISION);
  cout.precision(PRECISION);
}

void setupFlags(int argc, char *argv[]) {
  bool opt = false;
  cout << "\t\t";
  if(argc > 1) {
    for(int i = 1; i < argc; i++) {
      graphicMode = graphicMode || strcmp(argv[i], (char *)"--graphic") == 0;
      debugMode = debugMode || strcmp(argv[i], (char *)"--debug") == 0;
      silentMode = silentMode || strcmp(argv[i], (char *)"--silent") == 0;

      // parse ALLMODE flag (-A, --all)
      if(strcmp(argv[i], (char *)"-A") == 0 || strcmp(argv[i], (char *)"--all") == 0) {
        cout << "fetching all icons...\n";
        for(int j=0; j<NMODS; j++)
          READFILES[j] = true;
        break;
      }

      // find other flags
      for(int j=0; j<NMODS; j++)  {// TODO: can't add same flag more than once
        if(strcmp(argv[i], (char *)FLAGNAMES[j]) == 0) {
          opt = true;
          READFILES[j] = true; 
          cout << "fetching " << FILELABELS[j] << "\n\t\t";
          break;
        }}
    }

    if(!opt) {
      for(int i=0; i<NMODS; i++)
        READFILES[i] = true;
    }
  } else {
    cout << "DEFAULT mode -- will scan everything!\n";
    for(int i=0; i<NMODS; i++)
      READFILES[i] = true;
  }
}

int printProgressBar(int i, int load) {
  if(((i+1)*100/Q) >= load) {
    cout << load << "% " << (load/10 == 0 ? " " : "") << "[";
    for(int j = 0; j < load; j+=DLOAD) cout << "==";
    for(int j = load; j < 100; j+=DLOAD)  cout << "  ";
    cout << "]" << endl;
    load+=DLOAD;
  }
  return load;
}

void getMapScreenshot(int i, double lat, double lng) {
  bash.open ("script.sh");
  bash << "#!/bin/bash\n"
    << "google-chrome "
    << " \'https://www.waze.com/es-419/livemap?zoom=" << ZOOM
    << "&lat=" << lat << "&lon=" << lng << "\' &> /dev/null\n"
    << (i == 0 ? "xdotool key F11\n" : "")
    << "sleep 3\n"  // TODO: make sleep a variable
    << "scrot " << IMGNAME << "\n";
  bash.close();
  chksyscall("chmod +x script.sh");
  chksyscall("./script.sh");
}

void writeMatches(double lat, double lng) {
  for(int i=4; i<NMODS; i++) {
    if(READFILES[i]) {
      char filename[50];
      strcpy(filename, DIR);
      strcat(filename, FILENAMES[i]);
      writeMatch(filename, FILELABELS[i], lat, lng, THRESHOLDS[i][0]);
    }
  }
}

void writeMatch(char *templname, char* label, double lat, double lng, double threshold) {
  struct Location loc;
  vector<Point> points;

  fetchPositions( (char*) IMGNAME, templname, &points, threshold );

  if(points.size() >= 1) {
    char filename[50];
    strcpy(filename, label);
    strcat(filename, "-wr.log");
    data.open(filename, ios::app);
    for(vector<Point>::const_iterator pos = points.begin(); pos != points.end(); ++pos) {
      double posX = pos->x+ICON/2;
      double posY = pos->y+ICON/2;

      stringstream click;
      click << "xdotool mousemove " << posX << " " << posY << " click 1";      
      chksyscall(click.str());
      // TODO: read commentary above

      getCoordinates(pos->x+ICON/2, pos->y+ICON/2, lat, lng, &loc);
      if(!silentMode) cout << "*** found " << label << " at " << lat << ", " << lng << "\n";
      data << currentDateTime() << "," << loc.lat << "," << loc.lng << endl;
    }
    
    data.close();
  }
}

void getCoordinates(int pixelLng, int pixelLat, double lat, double lng, void *res) {
  // image's origin coordinates
  double oLat = lat + DLAT/2 ;
  double oLng = lng - DLNG/2;

  double resLat = oLat - DLAT*((double) pixelLat/(WINH-BARH));
  double resLng = oLng + DLNG*((double) pixelLng/WINW);

  struct Location * locRes = (struct Location *) res;
  locRes->lat = resLat;
  locRes->lng = resLng;
}

void chksyscall(string line) {
  int status = system(line.c_str());
  if (status < 0) {
    cout << "Error: " << strerror(errno) << '\n';
    clean();
    exit(-1);
  }
  else
  {
    if (!WIFEXITED(status)) {
      cerr << "\nCall to " << line << " was interrupted. Exiting program...\n";
      clean();
      exit(EXIT_FAILURE);
    }
  }
}

void fillMapCol(double iniLat, double iniLng, int n) {
  map[gidx].lat = iniLat;
  map[gidx].lng = iniLng;

  for(int i = 0; i < n; i++) {
    map[++gidx].lat = map[gidx-1].lat - DLAT;
    map[gidx].lng = iniLng;
  }
}

void generateMap() {
  gidx = 0;
  fillMapCol(4.8196, -74.02588, 11);
  fillMapCol(4.7980, -74.04648, 13);
  fillMapCol(4.7635, -74.06708, 18);
  fillMapCol(4.7569, -74.08768, 23);
  fillMapCol(4.7569, -74.10828, 25);
  fillMapCol(4.7421, -74.12888, 19);
  fillMapCol(4.6989, -74.14948, 15);
  fillMapCol(4.6532, -74.17008, 8);
  fillMapCol(4.6424, -74.19068, 6);
  fillMapCol(4.6316, -74.21128, 5);
}

string currentDateTime() {
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);

    return buf;
}

void signalHandler( int signum ) {
   cout << "Interrupt signal (" << signum << ") received.\n";
   clean();
   exit(signum);
}

void clean() {
  cout << "* Closing file streams... ";
  bash.close();
  cout << "done.\n";

  cout << "* Deleting auxiliary files... ";
  chksyscall("if [ -f ./screenshot.png ]; then \nrm screenshot.png \nfi");
  chksyscall("if [ -f ./script.sh ]; then \nrm script.sh \nfi");
  cout << "done." << endl;
}

bool matches(float f, double threshold) {
  if( match_method  == CV_TM_SQDIFF || match_method == CV_TM_SQDIFF_NORMED ) {
    return (f <= threshold);
  } else {
    return (f >= threshold);
  }
}

void fetchPositions( char* imgname, char* templname, void *out, double threshold ) {
  /// Load image and template
  Mat img_display;

  vector<Point> *vec = (vector<Point> *) out;
  img = imread( imgname );
  templ = imread( templname, 1 );

  if( img.empty() ) {
    cout << "Could not open or find the image " << imgname << endl;
    vec->push_back(Point(-1,-1));
    return ;
  }

  /// Create windows
  if(graphicMode || debugMode) {
    namedWindow( image_window, CV_WINDOW_AUTOSIZE );
    img.copyTo( img_display );
  }

  /// Create the result matrix
  int result_cols =  img.cols - templ.cols + 1;
  int result_rows = img.rows - templ.rows + 1;

  matchTemplate( img, templ, result, match_method );

  float max = 0;
  bool found;
  for(int i = 0; i < result_rows; i++) {
    found = false;
    for(int j = 0; j < result_cols; j++) {
      float res = result.at<float>(i,j);
      if(res > max) max = res;
      if(matches(res, threshold)) {
        if(graphicMode || debugMode) {
          found = true;
          cout << "(" << j << ", " << i << "): " << res << endl;
          rectangle( img_display,
            Point(j, i),
            Point(j + templ.cols , i + templ.rows ),
            Scalar::all(0), 2, 8, 0
          );
        }

        vec->push_back(Point(j, i));
      }
    }
  }

  if(debugMode) cout << max << endl;

  if((graphicMode && found) || debugMode) {
    imshow( image_window, img_display );
    waitKey(0);
    destroyAllWindows();
  }
}