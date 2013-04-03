#include "query.h"

bool urltable_create(string fnames, unordered_map<unsigned int, UrlTable> &urlTable, unsigned int &didCnt, unsigned int &avgLen);
bool lex_tree_create(char *fname, char *chkfname, unordered_map<string, LexNode> &lexTree);
//void docid_mapper_create(char *fname, unordered_map<unsigned int, docNode> &docTree);
void query(string keywords, char **argv, unordered_map<string, LexNode> &lexTree,
           unordered_map<unsigned int, UrlTable> &urlTable,
           unsigned int didCnt, unsigned int avgLen, unsigned int topk);
bool openLists(string keywords, vector<Record> &lps, unordered_map<string, LexNode> &lexTree, char* listfilename);
bool closeLists(vector<Record> &lps);
long nextGEQ(Record &lp, unsigned int k);
unsigned int getFreq(Record &lp);
void getBM25(unordered_map<unsigned int, UrlTable> &urlTable,
             vector<Record> &lps, Result &result, unsigned int didCnt, 
             unsigned int avgLen);
void printResults(unordered_map<unsigned int, UrlTable> &urlTable, 
                  vector<Record> &lps,
                  priority_queue<Result, vector<Result>, BM25Comparator> &resultSet,
                  unsigned int topk);
string createSnippet(vector<Record> &lps, Result &result, UrlTable &ut);
Hit getContextAndPosition(vector<char> list, int offset);

char *memAlloc(gzFile* fileName, int size, bool readAll = true);
unsigned int vb_decode(Record &listbuf);
string get_fnames(char* path);
string trim_tags(string);
bool load_next_10_urltable_names(unordered_map<unsigned int, UrlTable> &urlTable,
                                 unsigned int &didCnt, unsigned int &avgLen);


double k = 1.2, b = 0.75; // parameters of BM25
stringstream urlss;

int main(int argc, char **argv){
  if(argc != 5){
    cout<<"Format: ./query lexicon i2list i2chunk urltable topk"<<endl;
    exit(1);
  }
  clock_t beg = clock();
  unsigned int topk = 10; // = argv[5]
  unordered_map<string, LexNode> lexTree;
  if(lex_tree_create(argv[1], argv[3], lexTree) == false){
    cerr<<"Error creating lex tree"<<endl;
    exit(1);
  }
  cout<<"Lexicon Cnt: "<<lexTree.size()<<endl;
  // for(auto itr = lexTree.begin(); itr!=lexTree.end(); ++itr)
  //   cout<<itr->first<<',';

  urlss<<get_fnames(argv[4]);
  unordered_map<unsigned int, UrlTable> urlTable;
  unsigned int didCnt = 0, avgLen = 0;
  // if(urltable_create(argv[4], urlTable, didCnt, avgLen) == false){
  //   exit(1);
  // }
  load_next_10_urltable_names(urlTable, didCnt, avgLen);

  cout<<"DocCnt: "<<didCnt<<","<<"avgLen: "<<avgLen<<endl;
  cout<<"loading time: "<<(double)(clock()-beg)/CLOCKS_PER_SEC<<endl;


  string keywords;
  cout<<endl<<"Keywords: ";
  while(getline(cin, keywords)){
    beg = clock();
    if(keywords == "__exit__") break;
    query(keywords, argv, lexTree, urlTable, didCnt, avgLen, topk);
    cout<<endl<<"Total time: "<<(double)(clock()-beg)/CLOCKS_PER_SEC<<endl;
    cout<<endl<<"Keywords: ";
  }
  //  query("pepsi", argv, lexTree, urlTable, didCnt, avgLen, topk);

  return 0;
}

void query(string keywords, char **argv, unordered_map<string, LexNode> &lexTree,
           unordered_map<unsigned int, UrlTable> &urlTable,
           unsigned int didCnt, unsigned int avgLen, unsigned int topk){
  
  if(urlTable.size() == 0){
    urlss.str(string());
    urlss<<get_fnames(argv[4]);
    if(!load_next_10_urltable_names(urlTable, didCnt, avgLen)){
      cerr<<"urlTable loading error"<<endl;
      return;
    }
  }

  vector<Record> lps; // save the lists
  if(!openLists(keywords, lps, lexTree, argv[2])) exit(1); 

  priority_queue<Result, vector<Result>, BM25Comparator> resultSet;
  long did = 0;
  unsigned resCnt = 0;
  while(lps.size() != 0){
    did = nextGEQ(lps[0], did);
    long d = did; // if there's only one keyword, put all records in the heap
    for(unsigned i = 1; i < lps.size() && did > -1 
          && (d = nextGEQ(lps[i], did)) == did; ++i);
    if( did == -1 || d == -1){
      break;
    }
    //  cout<<d<<":"<<did<<endl;
    if (d > did) {
      did = d;
      // position skipping is determined by did?d
      for(unsigned i = 0; i < lps.size(); ++i){
        unsigned f = getFreq(lps[i]);
        lps[i].pos += f*2;
      }
    }
    else if( d == did){

      ++resCnt;

      Result result;
      result.did = d;
      for(unsigned i = 0; i < lps.size(); ++i){
        unsigned f = getFreq(lps[i]);
        //cout<<lps[i].pos<<"-"<<f<<endl;
        result.posRes.push_back(PosRes(f, lps[i].pos));
        lps[i].pos += f*2; // pos pointer jumps through the position info
      }
      getBM25(urlTable, lps, result, didCnt, avgLen);
      
      if (resultSet.size() < topk) resultSet.push(result);
      else{
        if(resultSet.top().bm25 < result.bm25){
          resultSet.pop();
          resultSet.push(result);
        }
      }
      did += 1; // go find the next same docid
    }
  }
  cout<<"query got: "<<resCnt<<endl;
  printResults(urlTable, lps, resultSet, topk);
  
  while(!resultSet.empty()) resultSet.pop();
  lps.clear();

}

bool lex_tree_create(char *lexname,char* chkfname, unordered_map<string, LexNode> &lexTree){
  gzFile* inlex =(gzFile*) gzopen(lexname,"r");
  if (!inlex) {
    cerr<<"Error opening "<<lexname<<":"<<strerror(errno)<<endl;
    return false;
  }
  // chkfname is a plain file. Just want to reuse memAlloc()
  gzFile* chkHandle =(gzFile*) gzopen(chkfname,"r");
  if (!chkHandle) {
    cerr<<"Error opening "<<chkfname<<":"<<strerror(errno)<<endl;
    return false;
  }

  long curPos = 0;
  unsigned int ft = 0, offset = 0, chkoffset = 0, curChkPos = 0;
  string word;
  char *lexData = memAlloc(inlex, 409600, true);
  stringstream lexbuf(lexData);
  char *chkData = memAlloc(chkHandle, 409600, true);
  string sChkData(chkData);

  while(lexbuf>>word>>ft>>offset>>chkoffset){
    //lexbuf>>word>>ft>>offset>>chkoffset; 
    ChkRec chkRec;
    //cout<<word<<","<<sChkData.size()<<","<<offset<<endl;
    stringstream buf(sChkData.substr(curChkPos, chkoffset));
    unsigned int docid = 0, docOffset = 0;

    while(buf>>docid>>docOffset){
      //buf>>docid>>docOffset;
      chkRec.chks.push_back(ChkInfo(docid, docOffset));
    }

    LexNode node(curPos, offset, ft);
    node.chkRec = chkRec;
    lexTree.insert(std::pair<string, LexNode>(word, node));
    curPos += offset;
    curChkPos += chkoffset;
  }

  gzclose((gzFile_s*)inlex);
  gzclose((gzFile_s*)chkHandle);

  return true;
}
bool load_next_10_urltable_names(unordered_map<unsigned int, UrlTable> &urlTable,
                                 unsigned int &didCnt, unsigned int &avgLen){
  urlTable.clear();
  int next = 10;
  string fname;
  string next_10_fnames;
  while(urlss>>fname && next--){
    next_10_fnames.append(fname + " ");
  }
  if(urltable_create(next_10_fnames.c_str(), urlTable, didCnt, avgLen) == false){
    return false;
  }
  return true;
}



bool openLists(string keywords, vector<Record> &lps,unordered_map<string, LexNode> &lexTree, char* listfilename){

  ifstream infile(listfilename, ifstream::binary);
  if(!infile.good()){
    cerr<<"Error opening "<<listfilename<<endl;
    return false;
  }

  stringstream ss(keywords);
  string kw;
  while(!ss.eof()){
    ss>>kw;
    auto itr = lexTree.find(kw);
    if ( itr == lexTree.end()){
      cerr<<kw<<" not found"<<endl;
      continue;
    }
    cout<<kw<<":"<<itr->second.pos<<','<<itr->second.offset<<","<<itr->second.ft<<endl;
    if(itr != lexTree.end()){
      infile.seekg(itr->second.pos);
      if(!infile.good()){
        cerr<<"Error seeking position for "<<kw<<endl;
        infile.clear(); //clear states
        continue;
      }

      Record record(itr->second, kw);
      record.lp.resize(itr->second.offset); //resize vs. reserve
      infile.read(&(record.lp[0]), itr->second.offset);
      if(!infile.good()){
        cerr<<"Error reading posting for "<<kw<<" "<< infile.rdstate()<<endl;
        infile.clear(); //clear states
        continue;
      }
      //cout<<record.lp.size()<<endl;
      lps.push_back(record);
    }
  }
  infile.close();
  return true;
}

unsigned int getFreq(Record &record){
  //  cout<<"freq: "<<static_cast<unsigned int>(static_cast<unsigned char>(record.lp[record.pos]))%256<<endl;
  if(record.pos+1 < record.lp.size())
    return static_cast<unsigned int>(static_cast<unsigned char>(record.lp[record.pos++]))%256;
  else return 0;
}

unsigned int vb_decode(Record &listbuf){
  unsigned int num = 0;
  for(; listbuf.pos < listbuf.lp.size(); ++listbuf.pos){
    //    cout<<(int)(unsigned char)listbuf.lp[listbuf.pos]<<",";
    if(static_cast<unsigned int>(static_cast<unsigned char>(listbuf.lp[listbuf.pos])) > 127){
      num |= listbuf.lp[listbuf.pos] & 0x7f; //clear the highest bit
      //cout<<num<<endl;
      num <<= 7;
    }
    else{
      num |= listbuf.lp[listbuf.pos];
      //cout<<"decode:"<<num<<endl;
      ++listbuf.pos;
      break;
    }
  }
  return num;
}
long nextGEQ(Record &lp, unsigned int k){
  while(lp.nodeInfo.chkRec.curChk < lp.nodeInfo.chkRec.chks.size() &&
        lp.nodeInfo.chkRec.chks[lp.nodeInfo.chkRec.curChk].docid < k){
    lp.pos += lp.nodeInfo.chkRec.chks[lp.nodeInfo.chkRec.curChk].pos;
    ++lp.nodeInfo.chkRec.curChk;
  }
  if (lp.nodeInfo.chkRec.curChk == lp.nodeInfo.chkRec.chks.size()){
    //cout<<"A list reaches its end"<<endl;
    return -1; // a record just reaches its end no need to continue the search
  }

  if (lp.pos == lp.lp.size()){
    //    cout<<" A list reaches its end"<<endl;
    return -1; // reach its end
  }
  unsigned int d = 0;
  do {

    d = vb_decode(lp);

    if(d >= k) break;

    unsigned freq = getFreq(lp);
    if( freq == 0 ){
      cout<<"freq = 0"<<endl;
      return -1;
    }
    lp.pos += freq*2; // every hit occupies 2bytes
  }while(lp.pos < lp.lp.size());

  return d;
}

string get_fnames(char* path){
  string cmd("ls ");
  cmd.append(path);
  cmd.append("/* | sort -k4 -t/ -n"); // sort by start docid
  // sort numerically (-n) on the third field (-k3) using 'p' 
  // as the field separator (-tp).
  //cout<<cmd<<endl;
  FILE *handle = popen(cmd.c_str(), "r");
  if(handle == NULL){
    cerr<<"popen error"<<endl;
    return NULL;
  }
  string fnames;
  char buff[128];
  while ((fgets(buff,127,handle)) != NULL){
    fnames.append(buff);
  } 
  pclose(handle);
  return fnames;
}
string trim_tags(string s){

 for(unsigned i=0; i<s.length(); i++)
   if(s[i] == '\n' || s.substr(i,2) == "  ") s.erase(i,1);

  int end = s.find_first_of(">");
  int beg = s.find_first_of("<");
  if(end != -1 && end < beg) s = s.substr(end+1);

  beg = s.find_first_of("<");
  while(beg != -1){
    end = s.find_first_of(">");
    if (end == -1) s = s.erase(beg, s.size()-1);
    s = s.erase(beg, end + 1 - beg);
    beg = s.find_first_of("<");
  }

  return s;
}

bool urltable_create(string fnames, unordered_map<unsigned int, UrlTable> &urlTable, 
                     unsigned int &didCnt, unsigned int &avgLen){
  //string fnames = get_fnames(fpath);
  stringstream ss(fnames);
  string lastPagePath;
  unsigned int curPagePos = 0; // covert the offset to real position
  //string strPath(fpath);

  while(!ss.eof()){
    string fname, line;
    ss>>fname;

    if(fname.find_first_of("-") == string::npos){
      //cout<<"urltable_create: bad urltable: "<<fname<<endl;
      continue; // ignore non-urltable files
    }
    igzstream urlHandle(fname.c_str());
    if(!urlHandle.good()){
      cerr<<"Error opening urltable "<<fname<<":"<<urlHandle.rdstate()<<endl;
      return false;
    }
    unsigned int did = 0, offset = 0;
    string url, pagePath;

    while(!urlHandle.eof()){
      getline(urlHandle, line);
      stringstream ssline(line);
      ssline>>did>>url>>pagePath>>offset;
      if(lastPagePath != pagePath){
        lastPagePath = pagePath;
        curPagePos = 0;
      }
      
      urlTable.insert(pair<unsigned int, UrlTable>(did, UrlTable(url, pagePath, curPagePos, offset)));
      curPagePos += offset;
      
      didCnt = did;
      if(avgLen == 0)
        avgLen += offset;
      else
        avgLen = (avgLen + offset)/2; 
    }
    urlHandle.close();
  } // while(!urlHandle.eof())
  didCnt += 1; // real docid starts from 0
  //cout<<didCnt<<"___"<<avgLen<<endl;
  return true;
}
void getBM25(unordered_map<unsigned int, UrlTable> &urlTable,
             vector<Record> &record, Result &result, unsigned int didCnt, 
             unsigned int avgLen){
  unsigned int lenOfdoc = 0;
  double bm25 = 0;
  auto itr = urlTable.find(result.did);
 
 if( itr != urlTable.end()) lenOfdoc = itr->second.pageSize;
  else lenOfdoc = avgLen;
  
  double K = k*((1-b) + b*lenOfdoc/avgLen);
  for(unsigned int i = 0; i < result.posRes.size(); ++i){
    bm25 += log((didCnt + record[i].nodeInfo.ft + 0.5)
                /(record[i].nodeInfo.ft + 0.5)) 
      * (k + 1)*(result.posRes[i].freq) / (K + result.posRes[i].freq);   
  }
  result.bm25 = bm25;
  // cout<< bm25<<",";
}

void printResults(unordered_map<unsigned int, UrlTable> &urlTable, vector<Record> &lps,
                  priority_queue<Result, vector<Result>, BM25Comparator> &resultSet,
                  unsigned int topk){
  stringstream output;
  unsigned int tmp = 0;
  while(tmp++ < topk && resultSet.size() > 0){    
    Result result = resultSet.top();
    resultSet.pop();
    output<<tmp<<" "<<result.bm25<<" ";
    auto itr = urlTable.find(result.did);
    if(itr == urlTable.end()){
      cout<<"printResults: docid not find in UrlTable"<<endl;
      continue;
    }
    output<<itr->second.url<<endl;
    //output<<"    "<<itr->second.pagePath<<" "<<itr->second.pos<<endl;
    output<<"    "<<createSnippet(lps, result, itr->second)<<endl;
    output<<"---------------------------------------------------"<<endl;
  }
  cout<<output.str();
}
string createSnippet(vector<Record> &lps, Result &result, UrlTable &ut){
  string filename = string("../indexer/"+ut.pagePath);
  gzFile* pageHandle =(gzFile*) gzopen(filename.c_str(),"r");
  if (!pageHandle) {
    cerr<<"Error opening "<<filename<<":"<<strerror(errno)<<endl;
    return string();
  }
  int readSize = 128+1;
  char *rawSpt = new char[readSize];
  //TODO: get the best snippet
  Hit hit = getContextAndPosition(lps[0].lp, result.posRes[0].startPos);
  gzseek((gzFile_s*)pageHandle, ut.pos + hit.wordPos, 0);

  stringstream ss;
  do{
    int r = gzread((gzFile_s*)pageHandle, rawSpt, readSize-1);
    if (r < readSize){
      if (!gzeof ((gzFile_s*)pageHandle)){
        const char * error_string;
        int err;
        error_string = gzerror ((gzFile_s*)pageHandle, & err);
        if (err) {
          fprintf (stderr, "Error: %s.\n", error_string);
          delete[] rawSpt;
          gzclose((gzFile_s*)pageHandle);
          return string();
        }   
      }
    }
    rawSpt[readSize-1] = '\0';
  }while(string(rawSpt).find_first_of(lps[0].keyword) < 0);

  ss<<rawSpt<<endl;
  //  string strSpt(rawSpt);
  delete[] rawSpt;
  gzclose((gzFile_s*)pageHandle);

  return trim_tags(ss.str()); //strSpt;
  
}

Hit getContextAndPosition(vector<char> list, int offset){
  //currently only return its postion
  short context = 0;
  short pos = 0;
  // char Record::context[7] = "BHIPTU";
  offset -= 2;
  do{
    offset +=2;
    context = list[offset]>>5 & 0x07;
    // skip the situation in which the keyword in in the link
  }while(context == 5 && offset < (signed)(list.size()-2));  
  //  cout<<"____"<<context<<endl;
  pos = static_cast<unsigned char>(list[offset]) & 0x1f;
  pos <<= 8;
  pos += static_cast<unsigned char>(list[offset+1]);
  
  return Hit(context, pos);
}
// find MinMax minimum
char *memAlloc(gzFile* fileName, int size, bool readAll){
    char *buffer=(char *)malloc(size);
    int oldSize=size;
    int count=0;             //The number of bytes that already read

    while (!gzeof((gzFile_s*)fileName)){ 
      int cur = gzread((gzFile_s*)fileName,buffer+count,oldSize);        
      if (cur < oldSize){
        if (!gzeof ((gzFile_s*)fileName)){
          const char * error_string;
          int err;
          error_string = gzerror ((gzFile_s*)fileName, & err);
          if (err) {
            fprintf (stderr, "Error: %s.\n", error_string);
            free(buffer);
            buffer = NULL;
            return buffer;
          }   
        }
      }
      count += cur;
      if(readAll == false)
        break;
      if (count == size){                    // Reallocate when buffer is full
        oldSize=size;
        size*=2;
        buffer=(char *)realloc(buffer,size);
      }
    }
    return buffer;
}
