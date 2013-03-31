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
#include <complex> // log
#include <algorithm>
#include <queue>
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
  string keyword;
  vector<char> lp;
  unsigned int pos; // record current working position
  LexNode nodeInfo;
  Record(LexNode ninfo, string kw, unsigned int p = 0){
    pos = p;
    nodeInfo = ninfo;
    keyword = kw;
  }
};

struct UrlTable{
  string url;
  string pagePath;
  unsigned int pos;
  unsigned int pageSize;
  UrlTable(string u, string pp, unsigned int ps, unsigned int psize){
    url = u;
    pagePath = pp;
    pos = ps;
    pageSize = psize;
  }
};

struct PosRes{
  int freq;
  unsigned int startPos;
  
  PosRes(int f, unsigned int sp){
    freq = f;
    startPos = sp;
  }
};

struct Result{
  unsigned int did = 0;
  double bm25 = 0;
  vector<PosRes> posRes;
};

class BM25Comparator{
 public:
  bool operator() (const Result& lhs, const Result&rhs) const{
    return lhs.bm25 < rhs.bm25;
  }
};
