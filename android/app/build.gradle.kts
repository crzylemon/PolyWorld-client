plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "games.polyworld.app"
    compileSdk = 35

    defaultConfig {
        applicationId = "games.polyworld.app"
        minSdk = 24
        targetSdk = 35
        versionCode = 26401
        versionName = "26.4.1"

        externalNativeBuild {
            cmake {
                val clientVer = (project.findProperty("CLIENT_VERSION") as String?) ?: "26.4.1"
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_ARM_NEON=TRUE",
                    "-DCLIENT_VERSION=${clientVer}"
                )
            }
        }
    }

    flavorDimensions += "device"
    productFlavors {
        create("mobile") {
            dimension = "device"
            ndk {
                abiFilters += listOf("armeabi-v7a", "arm64-v8a", "x86_64")
            }
            externalNativeBuild {
                cmake {
                    abiFilters += listOf("armeabi-v7a", "arm64-v8a", "x86_64")
                }
            }
        }
        create("quest") {
            dimension = "device"
            applicationIdSuffix = ".quest"
            versionNameSuffix = "-quest"
            minSdk = 29
            ndk {
                abiFilters += "arm64-v8a"
            }
            externalNativeBuild {
                cmake {
                    arguments += "-DPW_QUEST=ON"
                    abiFilters += "arm64-v8a"
                }
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
        debug {
            isDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        prefab = true
    }

    // Pack the shared engine asset tree into the APK (AAssetManager root).
    // Engine paths like "assets/stud.png" strip the "assets/" prefix on Android.
    sourceSets {
        getByName("main") {
            assets.srcDirs("../../assets")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    packaging {
        jniLibs {
            // Keep c++_shared if the NDK pulls it in
            keepDebugSymbols += listOf("**/*.so")
        }
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.games:games-activity:3.0.5")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    "questImplementation"("org.khronos.openxr:openxr_loader_for_android:1.1.41")
}
