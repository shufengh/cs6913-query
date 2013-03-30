#include <fstream>
#include <unordered_map>
#include <string>
#include <vector>
#include <sstream>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <zlib.h>
using namespace std;

struct ChkInfo{ // save the chunk pair [docid pos]
  unsigned int docid;
  unsigned int pos;
  ChkInfo(unsigned int did=0, unsigned int p=0){
    docid = did;
    pos = p;
  }
};
struct ChkRec{
  unsigned int curChk;
  vector<ChkInfo> chks;
  ChkRec(unsigned int cc = 0){
    curChk = cc;
  }
};


struct LexNode{ // save the lexicon and help info
  unsigned int pos;
  unsigned int offset;
  unsigned int ft;
  ChkRec chkRec;
  LexNode( unsigned int p=0, unsigned int of=0, unsigned int f = 0){
    pos = p;
    offset = of;
    ft = f;
    //    chkRec = cr;
  }
};
struct Record{
  vector<char> lp;
  unsigned int pos; // record current working position
  LexNode nodeInfo;
  Record(LexNode ninfo, unsigned int p = 0){
    pos = p;
    nodeInfo = ninfo;
  }
};
