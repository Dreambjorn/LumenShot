#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hdrshot {

struct HdrContentMask {
    std::vector<std::uint8_t> amounts;
    unsigned regions=0;
    std::uint64_t sdr_pixels=0,blended_pixels=0;
};

struct MaskRows {
    template<class Body> void operator()(unsigned height,const Body& body) const {body(0,height);}
};

// Flat, bounded SDR islands are evidence; dark pixels alone are not.
template<class Read,class Rows=MaskRows> HdrContentMask analyze_content_mask(unsigned width,unsigned height,double white,
        const Read& read,const Rows& run_rows=Rows{}) {
    constexpr unsigned tile=8;
    HdrContentMask result;
    if(width<32 || height<32) return result;
    const unsigned columns=(width+tile-1)/tile,rows=(height+tile-1)/tile;
    std::vector<std::uint8_t> candidates(std::size_t(columns)*rows);
    const auto distance=[](const auto& a,const auto& b) {
        return std::max({std::abs(double(a.r)-b.r),std::abs(double(a.g)-b.g),std::abs(double(a.b)-b.b)});
    };
    const double reference=white/80,tolerance=reference*.0001;
    const auto sdr=[&](const auto& pixel) {
        return std::min({pixel.r,pixel.g,pixel.b})>=-tolerance
            && std::max({pixel.r,pixel.g,pixel.b})<=reference*1.01;
    };
    run_rows(rows,[&](unsigned first,unsigned last) {
        for(unsigned ty=first;ty<last;++ty) for(unsigned tx=0;tx<columns;++tx) {
            const unsigned left=tx*tile,top=ty*tile,right=std::min(width,left+tile),bottom=std::min(height,top+tile);
            unsigned horizontal=0,vertical=0;bool eligible=true;
            for(unsigned y=top;y<bottom && eligible;++y) for(unsigned x=left;x<right;++x) {
                const auto& pixel=read(x,y);
                if(!sdr(pixel)) {eligible=false;break;}
                if(x>left) horizontal+=distance(pixel,read(x-1,y))<=tolerance;
                if(y>top) vertical+=distance(pixel,read(x,y-1))<=tolerance;
            }
            candidates[std::size_t(ty)*columns+tx]=eligible
                && horizontal*10>=7*(right-left-1)*(bottom-top)
                && vertical*10>=7*(right-left)*(bottom-top-1);
        }
    });
    std::vector<unsigned> pending;
    for(unsigned index=0;index<candidates.size();++index) {
        if(candidates[index]!=1) continue;
        pending.clear();pending.push_back(index);candidates[index]=2;
        unsigned left=index%columns,right=left+1,top=index/columns,bottom=top+1;
        for(std::size_t next=0;next<pending.size();++next) {
            const unsigned current=pending[next],x=current%columns,y=current/columns;
            left=std::min(left,x);right=std::max(right,x+1);top=std::min(top,y);bottom=std::max(bottom,y+1);
            const auto visit=[&](unsigned neighbor) {
                if(candidates[neighbor]==1) {candidates[neighbor]=2;pending.push_back(neighbor);}
            };
            if(x) visit(current-1);
            if(x+1<columns) visit(current+1);
            if(y) visit(current-columns);
            if(y+1<rows) visit(current+columns);
        }
        if(!left || !top || right==columns || bottom==rows || right-left<3 || bottom-top<3
           || pending.size()*2<std::size_t(right-left)*(bottom-top)) continue;
        const bool complete=pending.size()==std::size_t(right-left)*(bottom-top);
        left*=tile;right*=tile;top*=tile;bottom*=tile;
        bool eligible=true;
        if(!complete) for(unsigned y=top;y<bottom && eligible;++y) for(unsigned x=left;x<right;++x)
            if(!sdr(read(x,y))) {eligible=false;break;}
        if(!eligible) continue;
        const auto extend=[&](bool vertical,unsigned position,int direction,unsigned first,unsigned last) {
            unsigned matching=0;
            for(unsigned n=first;n<last;++n) {
                const auto& pixel=vertical?read(position,n):read(n,position);
                const auto& neighbor=vertical?read(unsigned(int(position)+direction),n):read(n,unsigned(int(position)+direction));
                if(!sdr(neighbor)) return false;
                matching+=distance(pixel,neighbor)<=tolerance;
            }
            return matching*4>=last-first;
        };
        for(unsigned n=0;n<tile-1 && left>0 && extend(true,left,-1,top,bottom);++n) --left;
        for(unsigned n=0;n<tile-1 && right<width && extend(true,right-1,1,top,bottom);++n) ++right;
        for(unsigned n=0;n<tile-1 && top>0 && extend(false,top,-1,left,right);++n) --top;
        for(unsigned n=0;n<tile-1 && bottom<height && extend(false,bottom-1,1,left,right);++n) ++bottom;
        if(!left || !top || right==width || bottom==height) continue;
        const auto weak_edge=[&](bool vertical,unsigned position,int direction,unsigned first,unsigned last) {
            unsigned strong=0;
            for(unsigned n=first;n<last;++n) {
                const auto& pixel=vertical?read(position,n):read(n,position);
                const auto& neighbor=vertical?read(unsigned(int(position)+direction),n):read(n,unsigned(int(position)+direction));
                strong+=distance(pixel,neighbor)>reference*.1;
            }
            return strong*2<last-first;
        };
        const bool feather_left=weak_edge(true,left,-1,top,bottom),feather_right=weak_edge(true,right-1,1,top,bottom);
        const bool feather_top=weak_edge(false,top,-1,left,right),feather_bottom=weak_edge(false,bottom-1,1,left,right);
        if(result.amounts.empty()) result.amounts.assign(std::size_t(width)*height,255);
        if(!feather_left && !feather_right && !feather_top && !feather_bottom) {
            for(unsigned y=top;y<bottom;++y)
                std::fill_n(result.amounts.begin()+std::size_t(y)*width+left,right-left,0);
        } else for(unsigned y=top;y<bottom;++y) for(unsigned x=left;x<right;++x) {
            unsigned distance_to_edge=tile;
            if(feather_left) distance_to_edge=std::min(distance_to_edge,x-left);
            if(feather_right) distance_to_edge=std::min(distance_to_edge,right-1-x);
            if(feather_top) distance_to_edge=std::min(distance_to_edge,y-top);
            if(feather_bottom) distance_to_edge=std::min(distance_to_edge,bottom-1-y);
            const double t=double(distance_to_edge)/tile,weight=t*t*(3-2*t);
            auto& amount=result.amounts[std::size_t(y)*width+x];
            amount=std::min(amount,std::uint8_t(std::lround((1-weight)*255)));
        }
        if(++result.regions==64) break;
    }
    for(const auto amount:result.amounts) {result.sdr_pixels+=amount==0;result.blended_pixels+=amount>0 && amount<255;}
    return result;
}

} // namespace hdrshot
