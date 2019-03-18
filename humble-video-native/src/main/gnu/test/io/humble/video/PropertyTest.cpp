/*******************************************************************************
 * Copyright (c) 2014, Andrew "Art" Clarke.  All rights reserved.
 *   
 * This file is part of Humble-Video.
 *
 * Humble-Video is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Humble-Video is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Humble-Video.  If not, see <http://www.gnu.org/licenses/>.
 *******************************************************************************/
/*
 * PropertyTest.cpp
 *
 *  Created on: Feb 2, 2012
 *      Author: aclarke
 */

#include <cstdlib>
#include <cstring>
#include <io/humble/ferry/Logger.h>
#include <io/humble/ferry/LoggerStack.h>
#include "PropertyTest.h"
#include <io/humble/video/Property.h>
#include <io/humble/video/VideoExceptions.h>
#include <io/humble/video/KeyValueBag.h>
#include <io/humble/video/Demuxer.h>

using namespace io::humble::ferry;
using namespace io::humble::video;

VS_LOG_SETUP(VS_CPP_PACKAGE);

PropertyTest :: PropertyTest()
{

}

PropertyTest :: ~PropertyTest()
{
}

void
PropertyTest :: setUp()
{
  
}

void
PropertyTest :: tearDown()
{
  
}

void
PropertyTest::testValgrindStrlenIssue()
{
  // This is a bug in FFmpeg which I fixed in our
  // captive build. The error crops up for BINARY
  // option types that have no data in them.
  // This test tries to ensure we have a patched FFmpeg.
  RefPointer<Configurable> c = Demuxer::make();

  {
    LoggerStack stack;
    stack.setGlobalLevel(Logger::LEVEL_ERROR, false);

    char* value = c->getPropertyAsString("cryptokey");
    if (value) free(value);
  }
}
void
PropertyTest :: testCreation()
{
  LoggerStack stack;
  stack.setGlobalLevel(Logger::LEVEL_WARN, false);
  RefPointer<Configurable> c = Demuxer::make();
  RefPointer<Property> property =  c->getPropertyMetaData("packetsize");
  VS_LOG_DEBUG("Name: %s", property->getName());
  VS_LOG_DEBUG("Description: %s", property->getHelp());
  TSM_ASSERT("should exist", property);
}

void
PropertyTest :: testIteration()
{
  LoggerStack stack;
  stack.setGlobalLevel(Logger::LEVEL_DEBUG, false);

  RefPointer<Configurable> c = Demuxer::make();

  int32_t numProperties = c->getNumProperties();
  TSM_ASSERT("", numProperties > 0);

  for(int32_t i = 0; i < numProperties; i++)
  {
    RefPointer <Property> property =  c->getPropertyMetaData(i);
    const char* name = property->getName();
    VS_LOG_DEBUG("Name: %s", name);
    VS_LOG_DEBUG("Description: %s", property->getHelp());
    VS_LOG_DEBUG("Default: %lld", property->getDefault());
    if (strcmp(name, "cryptokey")==0)
      continue;
    Property::Type type = property->getType();
    switch (type) {
      case Property::PROPERTY_INT:
      case Property::PROPERTY_INT64:
        VS_LOG_DEBUG("Current value (long)    : %lld", c->getPropertyAsLong(name));
        break;
      case Property::PROPERTY_DOUBLE:
      case Property::PROPERTY_FLOAT:
        VS_LOG_DEBUG("Current value (double)  : %f", c->getPropertyAsDouble(name));
        break;
      case Property::PROPERTY_STRING:
        {
          char* value=c->getPropertyAsString(name);
          VS_LOG_DEBUG("Current value (string)  : %s", value);
          if (value) free(value);
        }
        break;
      case Property::PROPERTY_RATIONAL:
        {
          RefPointer<Rational> rational = c->getPropertyAsRational(name);
          VS_LOG_DEBUG("Current value (rational): %f", rational->getValue());
        }
        break;
      case Property::PROPERTY_IMAGE_SIZE:
      case Property::PROPERTY_PIXEL_FMT:
      case Property::PROPERTY_SAMPLE_FMT:
      case Property::PROPERTY_VIDEO_RATE:
      case Property::PROPERTY_DURATION:
      case Property::PROPERTY_COLOR:
      case Property::PROPERTY_CHANNEL_LAYOUT:
        // we need to implement these, but fall through for now.
        VS_LOG_DEBUG("Current value not supported");
        break;
      default:
        break;
    }
  }
}

void
PropertyTest :: testSetMetaData()
{
  LoggerStack stack;
  stack.setGlobalLevel(Logger::LEVEL_ERROR, false);

  RefPointer<Configurable> c = Demuxer::make();
  RefPointer<KeyValueBag> dict = KeyValueBag::make();
  RefPointer<KeyValueBag> unset = KeyValueBag::make();
  const char* realKey = "packetsize";
  const char* fakeKey = "not-a-valid-key-no-way-all-hail-zod";
  const char* realValue = "1000";
  const char* fakeValue = "1025";
  dict->setValue(realKey, realValue);
  dict->setValue(fakeKey, fakeValue);

  TSM_ASSERT("", dict->getNumKeys() == 2);
  TSM_ASSERT("", unset->getNumKeys() == 0);

  c->setProperty(dict.value(), unset.value());

  TSM_ASSERT("", c->getPropertyAsLong(realKey) == 1000);

  // make sure the fake isn't there.
  TS_ASSERT_THROWS(c->getPropertyMetaData(fakeKey), PropertyNotFoundException);

  // now make sure the returned dictionary only had the fake in it.
  TSM_ASSERT("", unset->getNumKeys() == 1);

  TSM_ASSERT("", strcmp(unset->getValue(fakeKey, KeyValueBag::KVB_NONE), fakeValue) == 0);
}

