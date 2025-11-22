function syncClassroomToBlynk() {
  var BLYNK_AUTH = "YOUR_BLYNK_AUTH_TOKEN";

  var missingCount = 0;
  var assignedCount = 0;
  var doneCount = 0;
  var validTasks = [];
  var classMissing = {}; // NEW: Track missing per class

  var now = new Date(
    new Date().toLocaleString("en-US", { timeZone: "Europe/London" })
  );

  Logger.log("--- SMART WORK SYNC ---");

  try {
    var response = Classroom.Courses.list({ courseStates: ["ACTIVE"] });
    if (!response.courses) return;

    for (var i = 0; i < response.courses.length; i++) {
      var course = response.courses[i];
      if (!classMissing[course.name]) classMissing[course.name] = 0; // Init

      try {
        // A. Map Assignments
        var workMap = {};
        var pageToken = null;
        do {
          var workResp = Classroom.Courses.CourseWork.list(course.id, {
            pageSize: 100,
            pageToken: pageToken,
          });
          if (workResp.courseWork) {
            for (var w = 0; w < workResp.courseWork.length; w++) {
              var wk = workResp.courseWork[w];
              workMap[wk.id] = {
                title: wk.title,
                dueDate: wk.dueDate,
                dueTime: wk.dueTime,
              };
            }
          }
          pageToken = workResp.nextPageToken;
        } while (pageToken);

        // B. Scan Submissions
        pageToken = null;
        do {
          var subResp = Classroom.Courses.CourseWork.StudentSubmissions.list(
            course.id,
            "-",
            { userId: "me", pageSize: 100, pageToken: pageToken }
          );

          if (subResp.studentSubmissions) {
            for (var s = 0; s < subResp.studentSubmissions.length; s++) {
              var sub = subResp.studentSubmissions[s];

              if (sub.state == "TURNED_IN" || sub.state == "RETURNED") {
                doneCount++;
              } else {
                var details = workMap[sub.courseWorkId];
                if (details) {
                  var isMissing = false;
                  var dObj = new Date(2099, 0, 1);

                  if (details.dueDate) {
                    dObj = new Date(
                      details.dueDate.year,
                      details.dueDate.month - 1,
                      details.dueDate.day
                    );
                    if (details.dueTime)
                      dObj.setHours(
                        details.dueTime.hours,
                        details.dueTime.minutes,
                        0
                      );
                    else dObj.setHours(23, 59, 59);
                  }

                  if (sub.late || now > dObj) {
                    isMissing = true;
                  }

                  if (isMissing) {
                    missingCount++;
                    classMissing[course.name]++; // NEW: Increment class count
                    validTasks.push({
                      title: details.title,
                      course: course.name,
                      date: dObj,
                      missing: true,
                    });
                  } else {
                    assignedCount++;
                    validTasks.push({
                      title: details.title,
                      course: course.name,
                      date: dObj,
                      missing: false,
                    });
                  }
                }
              }
            }
          }
          pageToken = subResp.nextPageToken;
        } while (pageToken);
      } catch (err) {
        Logger.log("Skipped: " + course.name);
      }
    }

    // Sort
    validTasks.sort(function (a, b) {
      if (a.missing && !b.missing) return -1;
      if (!a.missing && b.missing) return 1;
      return a.date - b.date;
    });

    // List String
    var listString = "";
    var firstMissingName =
      validTasks.length > 0 && validTasks[0].missing
        ? validTasks[0].title
        : "None";

    for (var k = 0; k < 5 && k < validTasks.length; k++) {
      if (k > 0) listString += "~";
      var t = (validTasks[k].title || "Task").replace(/[|~]/g, "");
      var c = (validTasks[k].course || "Class").replace(/[|~]/g, "");
      var dStr = "No Date";
      if (validTasks[k].date.getFullYear() < 2090)
        dStr =
          validTasks[k].date.getDate() +
          "/" +
          (validTasks[k].date.getMonth() + 1);
      listString += t + "|" + c + "|" + dStr;
    }

    // NEW: Build Class Data String (Format: "Maths:2|English:0|...")
    var classString = "";
    var classNames = Object.keys(classMissing);
    for (var c = 0; c < classNames.length; c++) {
      if (c > 0) classString += "|";
      classString += classNames[c] + ":" + classMissing[classNames[c]];
    }

    Logger.log(
      "✅ FINAL -> M:" + missingCount + " | ClassData: " + classString
    );

    var url =
      "https://blynk.cloud/external/api/batch/update?token=" +
      BLYNK_AUTH +
      "&V0=" +
      missingCount +
      "&V1=" +
      assignedCount +
      "&V2=" +
      doneCount +
      "&V3=" +
      encodeURIComponent(firstMissingName) +
      "&V4=" +
      encodeURIComponent(listString) +
      "&V5=" +
      encodeURIComponent(classString); // NEW V5
    UrlFetchApp.fetch(url);
  } catch (e) {
    Logger.log("ERROR: " + e.toString());
  }
}
