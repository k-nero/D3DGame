//
// Created by thanh.nguyen on 18/8/26.
//
#pragma once
#ifndef ENGINE_API_H
#define ENGINE_API_H
#if defined(ENGINE_SHARED)
    #if defined(_WIN32)
        #if defined(ENGINE_BUILDING)
            #define ENGINE_API __declspec(dllexport)
        #else
            #define ENGINE_API __declspec(dllimport)
        #endif
    #else
        #define ENGINE_API __attribute__((visibility("default")))
    #endif
#else
    #define ENGINE_API
#endif
#endif //ENGINE_API_H
