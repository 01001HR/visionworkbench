// __BEGIN_LICENSE__
// Copyright (C) 2006, 2007 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__


#include <cxxtest/TestSuite.h>
#include <string>
#include <vw/Core.h>
#include <vw/Image/PixelTypes.h>
#include <boost/type_traits.hpp>
#include <boost/utility/enable_if.hpp>

using namespace std;
using namespace vw;

typedef int T;
struct FuncT : ReturnFixedType<int> {
  int operator() (int a) const {return a;}
  int operator() (int a, int b) const {return a+b;}
};
typedef short Arg1T;
typedef int Arg2T;
typedef int ArgT;

typedef int ResultT;
typedef PixelRGBA<int> PixelT;

#include "TestInstantiateFreeList.hh"

class TestInstantiateCoreFree : public CxxTest::TestSuite
{
  public: void test_inst() {}
};