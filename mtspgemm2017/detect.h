#include "CSC.h"
#include "utility.h"
#include "IntervalTree.h"
#include "BitMap.h"
#include <omp.h>
#include <fstream>
#include <iostream>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#define KMER_LENGTH 21
using namespace std;

// compute the number of true overlapping reads
double TrueOverlapping(ifstream & ifs) {

    vector<Interval<int>> intervals;
    vector<Interval<int>> queries;
    vector<Interval<int>>::iterator q;
    double trueoverlaps;

    if(ifs.is_open()) {

        int id;
        int st;
        int en;
        
        while(ifs >> id >> st >> en) {
            
            intervals.push_back(Interval<int>(st, en, id));
            queries.push_back(Interval<int>(st, en, id));
        }

    } else std::cout << "error opening the ifs" << endl;

    IntervalTree<int> tree;
    vector<size_t> treecounts;

    tree = IntervalTree<int>(intervals);

    for (q = queries.begin(); q != queries.end(); ++q) 
    {
        vector<Interval<int>> results;
        tree.findOverlapping(q->start, q->stop, results);
        treecounts.push_back(results.size());
    }

    for(size_t t = 0; t < treecounts.size(); ++t) {
        trueoverlaps = trueoverlaps + (double)treecounts[t];
    }

    return trueoverlaps;

}

// compute the distance between two k-mers on the same read
template <typename IT>
double ComputeDistance(IT & fst, IT & snd) {

    double delta;
    double min, max, diff;

    //#pragma omp parallel for
    //cout << "i =" << fst[i] << ", j = " << snd[j] << endl;

    if(fst < snd) {
        min = (double)fst;
        max = (double)snd;
    } else {
        max = (double)fst;
        min = (double)snd;
    }

    diff = max-min;
    delta = diff; 

    return delta;
}    

// find when a k-mers pair represents an evidence of potential overlap or not
bool PotentialOverlap(double & dfst, double & dsnd) {

    double dmin, dmax; // to define the shortest and longest delta
    double Lmin, Lmax; // to compute the shortest length of the longest delta and the longest length of the shortes delta 
    bool region = false;
    double p_ins = 1.090;
    double p_del = 0.955;
    
    if(dfst <= dsnd) { 
        dmin = dfst; 
        dmax = dsnd;
    } else {
        dmax = dfst;
        dmin = dsnd;
    }
    
    Lmax = dmin/p_del; // maximum length of the shortest delta given the probability of deletion
    Lmin = dmax/p_ins; // minimum length of the longest delta given the probability of insertion
    
    if(Lmax > Lmin) {
        region = true;
    } else region = false;

    return region;
}

// compute the overlap length between potential overlapping reads pairs
template <typename IT>
size_t ComputeLength(map<size_t, pair<size_t, size_t>> & ifsmap, IT & col, IT & row) {

    size_t alignment_length = 0;
    std::map<size_t, pair<size_t, size_t>>::iterator jit;
    std::map<size_t, pair<size_t, size_t>>::iterator iit;

    jit = ifsmap.find(col); // col index
    iit = ifsmap.find(row); // row index
    
    if(iit->second.first < jit->second.first) {
        if(iit->second.second > jit->second.first) {
            alignment_length = min((iit->second.second - jit->second.first), (jit->second.second - jit->second.first));
        }
    }
    else if (iit->second.first > jit->second.first) {
        if(jit->second.second > iit->second.first) {
            alignment_length = min((jit->second.second - iit->second.first), (iit->second.second - iit->second.first));
        }
    } else { 
        alignment_length = min((jit->second.second - iit->second.first), (iit->second.second - iit->second.first)); 
    } 

    return alignment_length;
}

template <class IT, class NT>
void DetectOverlap(const CSC<IT,NT> & A) 
{
    std::ifstream ifs("test_01.axt"); // it would be better to make this reading more general
    std::map<size_t, pair<size_t, size_t>> ifsmap;
    std::vector<pair<size_t, pair<size_t, size_t>>>::iterator nit;
    std::vector<pair<size_t, pair<size_t, size_t>>>::iterator it;
    double di; // k-mers distances on read i
    double dj; // k-mers distances on read j
    double overlapcount = 0, truepositive = 0, prefilter = 0;
    size_t var, alignment_length;
    bool same;
    double track = 0;

    // creation reads map from axt file to be used to find potential overlapping reads pairs 
    if(ifs.is_open()) {
        int id;
        int st;
        int en;
        std::pair<size_t, size_t> values;
        while(ifs >> id >> st >> en) {
            size_t key = {id};
            size_t start = {st};
            size_t end = {en};
            values = make_pair(start, end);
            ifsmap[key] = values;
        }
    } else std::cout << "error opening the ifs" << endl;

    ifs.clear();
    ifs.seekg(0, ios::beg);

    double trueoverlapping = TrueOverlapping(ifs); // computing true overlapping reads pairs from fastq
    ifs.close();

    // filtering on reads pairs to keep just potential overlapping ones
    for(size_t i = 0; i < A.cols; ++i) 
    { 
        for(size_t j = A.colptr[i]; j < A.colptr[i+1]; ++j) 
        { 
            //if(i != A.rowids[j]) 
            //{
            if(A.values[j]->size() > 1)
            { 
                track = 0;
                var = A.values[j]->size();
    
                for(it = A.values[j]->begin(); it != A.values[j]->end(); ++it)     
                {
                    for(nit = A.values[j]->begin(); nit != A.values[j]->end(); ++nit)     
                    {
                        if(it->first != nit->first) 
                        {
                            same = false; 
        
                            di = ComputeDistance(it->second.first, nit->second.first);
                            dj = ComputeDistance(it->second.second, nit->second.second);
        
                            same = PotentialOverlap(di, dj); // compute evidence of potential overlap for each k-mers pair
        
                            if(same == true)
                                track++; // keep track of any potential overlap
                        }
                    }
                }
    
                if(track >= (double)var/2) // keep the pair if it shows at least one evidence of potential overlap
                {    
                    overlapcount++;
                    alignment_length = ComputeLength(ifsmap, i, A.rowids[j]); // compute the overlap length between potential overlapping reads pairs
                    
                    if(alignment_length >= KMER_LENGTH) 
                    {    
                        truepositive++;
                    }
                }
            } 
            else if(A.values[j]->size() == 1)
            {
                overlapcount++;
                alignment_length = ComputeLength(ifsmap, i, A.rowids[j]); 
                
                if(alignment_length >= KMER_LENGTH) 
                {    
                    truepositive++;
                }
            }
            prefilter++;
            //}
        }
    }

    cout << "Total number of reads pairs at the beginning (|| O ||) = " << prefilter << endl;
    cout << "True overlapping reads pairs (|| A ||) = " << trueoverlapping << endl;
    cout << "Potentially overlapping read pairs (|| A' ||) = " << overlapcount << endl;
    cout << "True positive reads pairs among the potential ones (|| A and A' ||) = " << truepositive << endl;
    cout << "Recall = " << truepositive/trueoverlapping << endl;
    cout << "\n-- A BIT LESS CONSERVATIVE --\n" << endl;
    
}