#include "query.h"

bool urltable_create(char *fname, unordered_map<unsigned int, UrlTable> &urlTable, unsigned int &didCnt, unsigned int &avgLen);
bool lex_tree_create(char *fname, char *chkfname, unordered_map<string, LexNode> &lexTree);
//void docid_mapper_create(char *fname, unordered_map<unsigned int, docNode> &docTree);
bool openLists(string keywords, vector<Record> &lps, unordered_map<string, LexNode> &lexTree, char* listfilename);
bool closeLists(vector<Record> &lps);
long nextGEQ(Record &lp, unsigned int k);
unsigned int getFreq(Record &lp);
void getBM25(unordered_map<unsigned int, UrlTable> &urlTable,
             vector<Record> &lps, Result &result, unsigned int didCnt, 
             unsigned int avgLen);
void printResults(unordered_map<unsigned int, UrlTable> &urlTable, vector<Record> &lps,
                  priority_queue<Result, vector<Result>, BM25Comparator> &resultSet,
                  unsigned int topk);

char *memAlloc(gzFile* fileName, int size, bool readAll = true);
unsigned int vb_decode(Record &listbuf);
string get_fnames(char* path);

double k = 1.2, b = 0.75; // parameters of BM25

int main(int argc, char **argv){
  if(argc != 5){
    cout<<"Format: ./query lexicon i2list i2chunk urltable topk"<<endl;
    exit(1);
  }

  unsigned int topk = 10; // = argv[5]
  unordered_map<string, LexNode> lexTree;
  if(lex_tree_create(argv[1], argv[3], lexTree) == false){
    cerr<<"Error creating lex tree"<<endl;
    exit(1);
  }
  unordered_map<unsigned int, UrlTable> urlTable;
  unsigned int didCnt = 0, avgLen = 0;
  if(urltable_create(argv[4], urlTable, didCnt, avgLen) == false){
    exit(1);
  }
  
  clock_t beg = clock();
  string keywords="hack";
  cout<<"Keywords: "<<keywords<<endl;  
  // while(getline(cin, keywords)){
  //   cout<<keywords<<endl;
  //     cout<<"close search engine"<<endl;
  //     break;
  //   }	./query test/lexicon.gz test/i2list test/i2chunk ./urltable/
  //   cout<<lexTree[keywords].pos<<','<<lexTree[keywords].offset<<endl;
  //   cout<<"Keywords: ";
  // }
  vector<Record> lps; // save the lists
  if(!openLists(keywords, lps, lexTree, argv[2])) exit(1); //TODO: change to break
  long did = 0;

  priority_queue<Result, vector<Result>, BM25Comparator> resultSet;
  while(lps.size() != 0){
    did = nextGEQ(lps[0], did);
    long d = did; // if there's only one keyword, put them all in the heap
    for(unsigned i = 1; i < lps.size() && did > -1 
          && (d = nextGEQ(lps[i], did)) == did; ++i);
    if( did == -1 || d == -1){
      break;
    }
    
    if (d > did) did = d;
    else{
      Result result;
      result.did = d;
      for(unsigned i = 0; i < lps.size(); ++i){
        int f = getFreq(lps[i]);
        result.posRes.push_back(PosRes(f, lps[i].pos));
        lps[i].pos += f*2; // pos pointer jumps through the position info
      }
      getBM25(urlTable, lps, result, didCnt, avgLen);
      
      if (resultSet.size() >= topk){
        //cout<<resultSet.top().bm25<<',';
        //resultSet.pop();
        resultSet.push(result);
      }
      else resultSet.push(result);
    }
  }
  cout<<"query got: "<<resultSet.size()<<endl;
  // for(auto itr = lps[0].begin(); itr != lps[0].end(); ++ itr)
  //   cout<<(char)(*itr);
  //int tmp = topk;
  // while(tmp-- && !resultSet.empty()){
  //       cout<<resultSet.top().bm25<<',';
  //       resultSet.pop();
  // }
  printResults(urlTable, lps, resultSet, topk);
  cout<<endl<<"Total time: "<<(double)(clock()-beg)/CLOCKS_PER_SEC<<endl;
  return 0;

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

  unsigned int curPos = 0, ft = 0, offset = 0, chkoffset = 0, curChkPos = 0;
  string word;
  char *lexData = memAlloc(inlex, 409600, true);
  stringstream lexbuf(lexData);
  char *chkData = memAlloc(chkHandle, 409600, true);
  string sChkData(chkData);

  while(!lexbuf.eof()){
    lexbuf>>word>>ft>>offset>>chkoffset; 
    ChkRec chkRec;
    //cout<<word<<","<<sChkData.size()<<","<<offset<<endl;
    stringstream buf(sChkData.substr(curChkPos, chkoffset));
    unsigned int docid = 0, docOffset = 0;

    while(!buf.eof()){
      buf>>docid>>docOffset;
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
  // while(lp.nodeInfo.chkRec.curChk < lp.nodeInfo.chkRec.chks.size() &&
  //       lp.nodeInfo.chkRec.chks[lp.nodeInfo.chkRec.curChk].docid < k){
  //   lp.pos += lp.nodeInfo.chkRec.chks[lp.nodeInfo.chkRec.curChk].pos;
  //   ++lp.nodeInfo.chkRec.curChk;
  // }
  // if (lp.nodeInfo.chkRec.curChk == lp.nodeInfo.chkRec.chks.size()){
  //   cout<<"A list reaches its end"<<endl;
  //   return -1; // a record just reaches its end no need to continue the search
  // }
  if (lp.pos == lp.lp.size()){
    //    cout<<" A list reaches its end"<<endl;
    return -1; // reach its end
  }
  unsigned int d = 0;
  do {
    d = vb_decode(lp);
    if(d >= k){
      break;
    }
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
  cmd.append("/");
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

bool urltable_create(char *fpath, unordered_map<unsigned int, UrlTable> &urlTable, 
                     unsigned int &didCnt, unsigned int &avgLen){
  string fnames = get_fnames(fpath);
  stringstream ss(fnames);
  string lastPagePath;
  unsigned int curPagePos = 0; // covert the offset to real position
  string strPath(fpath);

  while(!ss.eof()){
    string fname, line;
    ss>>fname;
    if(fname.find("-") == string::npos)
      break; // ignore non-urltable files
    ifstream urlHandle(strPath + fname);
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
  auto itr = urlTable.find(result.did);
 
 if( itr != urlTable.end()) lenOfdoc = itr->second.pageSize;
  else lenOfdoc = avgLen;
  
  double K = k*((1-b) + b*lenOfdoc/avgLen);

  double bm25 = 0;
  for(unsigned int i = 0; i < result.posRes.size(); ++i){
    bm25 += log((didCnt + record[i].nodeInfo.ft + 0.5)
                /(record[i].nodeInfo.ft + 0.5)) 
      * (k + 1)*(result.posRes[i].freq) / (K + result.posRes[i].freq);   
  }
  result.bm25 = bm25;
  // cout<< bm25<<",";
}

void printResults(unordered_map<unsigned int, UrlTable> &urlTable, vector<Record> &lps,
                  priority_queue<Result, vector<Result>, BM25Comparator> &resultSet, unsigned int topk){
  stringstream output;
  while(topk--){    
    Result result = resultSet.top();
    resultSet.pop();
    output<<result.bm25<<" ";
    auto itr = urlTable.find(result.did);
    if(itr == urlTable.end()){
      cout<<"printResults: docid not find in UrlTable"<<endl;
      continue;
    }
    output<<itr->second.url<<endl;
    output<<"    "<<itr->second.pagePath<<" "<<itr->second.pos<<endl;
    
  }
  cout<<output.str();
}
string createSnippet(Result &result, UrlTable &ut){
  gzFile* pageHandle =(gzFile*) gzopen(string("../indexer/"+ut.pagePath).c_str(),"r");
  if (!pageHandle) {
    cerr<<"Error opening "<<string("../indexer/"+ut.pagePath)<<":"<<strerror(errno)<<endl;
    return string();
  }
  // get the best snippet

  gzclose((gzFile_s*)pageHandle);
  return string();
}
