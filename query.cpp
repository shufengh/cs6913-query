#include "query.h"


bool lex_tree_create(char *fname,char *chkfname, unordered_map<string, LexNode> &lexTree);
//void docid_mapper_create(char *fname, unordered_map<unsigned int, docNode> &docTree);
bool openLists(string keywords, vector<Record> &lps, unordered_map<string, LexNode> &lexTree, char* listfilename);
bool closeLists(vector<Record> &lps);
long nextGEQ(Record &lp, unsigned int k);
unsigned int getFreq(Record &lp);
char *memAlloc(gzFile* fileName, int size, bool readAll = true);
unsigned int vb_decode(Record &listbuf);

int main(int argc, char ** argv){
  if(argc != 5){
    cout<<"Format: ./query lexicon i2list i2chunk urltable"<<endl;
    exit(1);
  }
  unordered_map<string, LexNode> lexTree;
  if(lex_tree_create(argv[1], argv[3], lexTree) == false){
    cerr<<"Error creating lex tree"<<endl;
    exit(1);
  }
  string keywords="yore";
  
  cout<<"Keywords: "<<keywords<<endl;
  
  // while(getline(cin, keywords)){
  //   cout<<keywords<<endl;
  //   if(keywords == "--exit--"){
  //     cout<<"close search engine"<<endl;
  //     break;
  //   }
  //   cout<<lexTree[keywords].pos<<','<<lexTree[keywords].offset<<endl;
  //   cout<<"Keywords: ";
  // }
  vector<Record> lps; // save the lists
  if(!openLists(keywords, lps, lexTree, argv[2])) exit(1); //TODO: change to break
  long did = 0;
  //  cout<<lps[0].lp<<endl;
  while(lps.size() != 0){
    did = nextGEQ(lps[0], did);
    long d = did; // if there's only one keywords, put them all in the heap
    for(unsigned i = 1; i < lps.size() && did > -1 && (d = nextGEQ(lps[i], did)) == did; ++i);
    if( did == -1 || d == -1){
      break;
    }
    cout<<d<<":"<<did<<endl;
    
    if (d > did) did = d;
    else{
      //cout<<d<<",";
    }
  }


  // for(auto itr = lps[0].begin(); itr != lps[0].end(); ++ itr)
  //   cout<<(char)(*itr);
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
  if (!inlex) {
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

      Record record(itr->second);
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
  unsigned int d = 0;
  do {
    d = vb_decode(lp);
    unsigned freq = getFreq(lp);
    cout<<"DocID:"<<d<<":"<<freq<<":"<<lp.pos<<" / "<<lp.lp.size()<<endl;
    if( freq == 0 ){
      cout<<"freq = 0"<<endl;
      return -1;
    }
    lp.pos += freq*2; // every hit occupies 2bytes

  }while(d < k && lp.pos < lp.lp.size());

  if (lp.pos == lp.lp.size()){
    cout<<" A list reaches its end"<<endl;
    return -1; // reach its end
  }
  return d;
}
